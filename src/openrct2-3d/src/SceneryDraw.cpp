/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "SceneryDraw.hpp"

#include "GlbMesh.hpp"
#include "GpuBudget.hpp"
#include "MeshDraw.hpp"
#include "ModelCache.hpp"
#include "ModelLoadQueue.hpp"
#include "ParkView.hpp"
#include "SceneryChunk.hpp"
#include "SceneryScale.hpp"
#include "SpriteQuad.hpp"
#include "ViewerScale.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/PlatformEnvironment.h>
#include <openrct2/core/File.h>
#include <openrct2/core/Path.hpp>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/FilterPaletteIds.h>
#include <openrct2/drawing/LightFX.h>
#include <openrct2/drawing/Palette.h>
#include <openrct2/drawing/RenderTarget.h>
#include <openrct2/interface/Cursors.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/interface/Window.h>
#include <openrct2/interface/WindowBase.h>
#include <openrct2/localisation/StringIds.h>
#include <openrct2/object/Object.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/Scenery.h>
#include <openrct2/world/TileElementsView.h>
#include <openrct2/world/tile_element/SmallSceneryElement.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace OpenRCT2::View3D::Park
{
    namespace
    {
        constexpr int32_t kMaxSpriteEdge = 1024;

        struct Batch
        {
            int32_t handle = -1;
            bool model = false;
            bool vegetation = false;
            std::vector<MeshInstance> instances;
        };

        struct SpriteKey
        {
            uint64_t image = 0;
            int face = 0;
            bool operator==(const SpriteKey& other) const
            {
                return image == other.image && face == other.face;
            }
        };

        struct SpriteKeyHash
        {
            size_t operator()(const SpriteKey& key) const
            {
                return std::hash<uint64_t>{}(key.image) ^ (static_cast<size_t>(key.face) << 1);
            }
        };

        struct FrameCull
        {
            bool haveCamera = false;
            float eyeX = 0.0f;
            float eyeY = 0.0f;
            float eyeZ = 0.0f;
            float focusX = 0.0f;
            float focusY = 0.0f;
            float planes[6][4] = {};
        };

        bool _dropSprites = false;

        std::unordered_map<SpriteKey, int32_t, SpriteKeyHash> _spriteHandles;
        std::unordered_set<std::string> _loggedUploadFail;
        std::vector<Batch> _batches;
        std::unordered_map<uint32_t, size_t> _modelBatches;
        std::unordered_map<int32_t, size_t> _spriteBatches;
        bool _loggedShader = false;
        std::vector<Drawing::PaletteIndex> _blit;
        std::vector<uint8_t> _rgba;
        SceneryChunkGrid _grid;
        bool _gridReady = false;
        std::vector<std::string> _typeIds;
        std::unordered_map<std::string, uint32_t> _typeIndex;
        std::unordered_map<uint32_t, int32_t> _gpu;
        std::unique_ptr<ModelLoadQueue> _loads;
        std::mutex _dirtyMutex;
        std::vector<std::pair<int32_t, int32_t>> _dirtyTiles;
        bool _dirtyAll = true;
        uint64_t _frame = 0;
        LruBudget _vram(kDefaultVramBudgetBytes);

        void onMapChanged(int32_t tileX, int32_t tileY)
        {
            if (gOpenRCT2Headless)
            {
                return;
            }
            std::lock_guard<std::mutex> guard(_dirtyMutex);
            if (tileX < 0 || tileY < 0)
            {
                _dirtyAll = true;
                _dirtyTiles.clear();
                _dropSprites = true;
                return;
            }
            // A long 2D session can invalidate the same tiles for many frames before 3D draws.
            if (_dirtyTiles.size() > 8192)
            {
                _dirtyAll = true;
                _dirtyTiles.clear();
                return;
            }
            _dirtyTiles.emplace_back(tileX, tileY);
        }

        uint32_t typeKeyFor(const std::string& identifier)
        {
            const auto found = _typeIndex.find(identifier);
            if (found != _typeIndex.end())
            {
                return found->second;
            }
            const uint32_t key = static_cast<uint32_t>(_typeIds.size());
            _typeIds.push_back(identifier);
            _typeIndex.emplace(identifier, key);
            return key;
        }

        void ensureLoads()
        {
            if (_loads != nullptr)
            {
                return;
            }
            _loads = std::make_unique<ModelLoadQueue>(
                [](const std::string& identifier, std::string& error) { return loadModelForDraw(identifier, {}, {}, error); },
                true);
        }

        std::vector<std::string> modelSourceDirs()
        {
            std::vector<std::string> dirs;
            IContext* context = GetContext();
            if (context == nullptr)
            {
                return dirs;
            }
            const IPlatformEnvironment& env = context->GetPlatformEnvironment();
            // Data dir wins over the user dir, matching prepareModelCache.
            dirs.push_back(Path::Combine(env.GetDirectoryPath(DirBase::openrct2), u8"object-models"));
            dirs.push_back(Path::Combine(env.GetDirectoryPath(DirBase::user), u8"object-models"));
            return dirs;
        }

        std::string modelCacheDir()
        {
            IContext* context = GetContext();
            if (context == nullptr)
            {
                return {};
            }
            return Path::Combine(context->GetPlatformEnvironment().GetDirectoryPath(DirBase::cache), u8"object-models");
        }

        void prepareSceneryModels()
        {
            const std::vector<std::string> dirs = modelSourceDirs();
            const std::string cache = modelCacheDir();
            prepareModelCache(dirs, cache, [](int32_t done, int32_t total, std::string_view) {
                IContext* context = GetContext();
                if (context != nullptr)
                {
                    context->SetProgress(static_cast<uint32_t>(done), static_cast<uint32_t>(total));
                }
            });
        }

        struct RegisterSceneryModels
        {
            RegisterSceneryModels()
            {
                SetPrepareSceneryModels(&prepareSceneryModels);
                SetMapChangedHook(&onMapChanged);
            }
        };

        const RegisterSceneryModels kRegisterSceneryModels;

        bool isVegetation(const SmallSceneryEntry& entry)
        {
            return entry.flags.has(SmallSceneryFlag::isTree) || entry.tool_id == CursorID::treeDown
                || entry.tool_id == CursorID::flowerDown;
        }

        uint8_t witherBand(const SmallSceneryEntry& entry, uint8_t age)
        {
            if (!entry.flags.has(SmallSceneryFlag::canWither))
            {
                return 0;
            }
            if (age >= kSceneryWitherAgeThreshold2)
            {
                return 2;
            }
            if (age >= kSceneryWitherAgeThreshold1)
            {
                return 1;
            }
            return 0;
        }

        void anchor(
            const TileCoordsXY& tile, const SmallSceneryElement& element, const SmallSceneryEntry& entry, float& x, float& y,
            float& z)
        {
            // Stored quadrant, not the view-rotated index the 2D painter adds.
            // Models and the X-sprite fallback share this point, and yaw is about it.
            sceneryAnchor(
                tile.x, tile.y, entry.flags.has(SmallSceneryFlag::occupiesFullTile), element.getSceneryQuadrant(), x, y);
            z = viewerZ(element.getBaseZ());
        }

        ImageId faceImage(const SmallSceneryElement& element, const SmallSceneryEntry& entry, int face)
        {
            ImageId image;
            if (element.isGhost())
            {
                image = ImageId().WithRemap(Drawing::FilterPaletteID::paletteGhost);
            }
            const uint8_t direction = static_cast<uint8_t>((element.getDirection() + face) & 3);
            ImageIndex index = entry.image + direction;
            index += static_cast<ImageIndex>(witherBand(entry, element.getAge())) * 4;
            image = image.WithIndex(index);
            return sceneryRemapImage(
                image, entry.flags.has(SmallSceneryFlag::hasPrimaryColour), static_cast<uint8_t>(element.getPrimaryColour()),
                entry.flags.has(SmallSceneryFlag::hasSecondaryColour), static_cast<uint8_t>(element.getSecondaryColour()),
                entry.flags.has(SmallSceneryFlag::hasTertiaryColour), static_cast<uint8_t>(element.getTertiaryColour()));
        }

        const Drawing::GamePalette& texturePalette()
        {
            return Drawing::LightFx::IsAvailable() ? Drawing::LightFx::GetPalette() : Drawing::gPalette;
        }

        uint64_t imageBits(const ImageId& image)
        {
            uint64_t bits = 0;
            static_assert(sizeof(ImageId) == sizeof(bits), "ImageId is the sprite cache key");
            std::memcpy(&bits, &image, sizeof(bits));
            return bits;
        }

        ImageId imageFromBits(uint64_t bits)
        {
            ImageId image;
            std::memcpy(&image, &bits, sizeof(image));
            return image;
        }

        int32_t spriteHandleForFace(uint64_t bits, int face)
        {
            const SpriteKey key{ bits, face };
            const auto found = _spriteHandles.find(key);
            if (found != _spriteHandles.end())
            {
                return found->second;
            }
            const ImageId image = imageFromBits(bits);
            const G1Element* g1 = GfxGetG1Element(image);
            if (g1 == nullptr || g1->width <= 0 || g1->height <= 0 || g1->width > kMaxSpriteEdge || g1->height > kMaxSpriteEdge)
            {
                _spriteHandles.emplace(key, -1);
                return -1;
            }
            const int32_t width = g1->width;
            const int32_t height = g1->height;
            _blit.assign(static_cast<size_t>(width) * static_cast<size_t>(height), Drawing::PaletteIndex::transparent);
            Drawing::RenderTarget rt;
            rt.bits = _blit.data();
            rt.width = width;
            rt.height = height;
            rt.pitch = 0;
            rt.zoom_level = ZoomLevel{ 0 };
            GfxDrawSpriteSoftware(rt, image, { -g1->xOffset, -g1->yOffset });

            _rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
            const Drawing::GamePalette& palette = texturePalette();
            for (size_t i = 0; i < _blit.size(); i++)
            {
                uint8_t* dst = _rgba.data() + i * 4;
                const Drawing::PaletteIndex index = _blit[i];
                if (index == Drawing::PaletteIndex::transparent)
                {
                    dst[0] = 0;
                    dst[1] = 0;
                    dst[2] = 0;
                    dst[3] = 0;
                    continue;
                }
                const Drawing::BGRAColour colour = palette[static_cast<uint8_t>(index)];
                dst[0] = colour.red;
                dst[1] = colour.green;
                dst[2] = colour.blue;
                dst[3] = 255;
            }
            const SpriteQuad quad = makeDirectionSpriteQuad(face, g1->width, g1->height, g1->xOffset, g1->yOffset);
            const int32_t handle = meshDrawUploadSprite(quad, _rgba.data(), width, height);
            _spriteHandles.emplace(key, handle);
            return handle;
        }

        void appendElement(SceneryChunk& chunk, const TileCoordsXY& tile, const SmallSceneryElement& element)
        {
            const SmallSceneryEntry* entry = element.getEntry();
            if (entry == nullptr || element.isInvisible())
            {
                return;
            }
            float ax;
            float ay;
            float az;
            anchor(tile, element, *entry, ax, ay, az);
            ChunkPlacement placement;
            placement.x = ax;
            placement.y = ay;
            placement.z = az;
            placement.yaw = modelYawRadians(element.getDirection());
            placement.vegetation = isVegetation(*entry);
            std::string identifier;
            const Object* object = ObjectEntryGetObject(ObjectType::smallScenery, element.getEntryIndex());
            if (object != nullptr)
            {
                identifier = std::string(object->GetIdentifier());
            }
            placement.modelCandidate = !identifier.empty() && modelIsValidated(identifier);
            if (placement.modelCandidate)
            {
                placement.typeKey = typeKeyFor(identifier);
            }
            // Keep the image identity for a later fallback. Do not upload here: a model
            // object, and anything not on screen, must not take an atlas slot.
            for (int face = 0; face < 4; face++)
            {
                const ImageId image = faceImage(element, *entry, face);
                placement.spriteImage[face] = imageBits(image);
                placement.spriteFace[face] = true;
            }
            const float declared = viewerZ(entry->height);
            placement.topZ = az + (declared > 0.25f ? declared : 0.25f);
            chunk.placements.push_back(placement);
        }

        void rebuildChunk(int32_t chunkX, int32_t chunkY)
        {
            SceneryChunk* chunk = _grid.at(chunkX, chunkY);
            if (chunk == nullptr)
            {
                return;
            }
            chunk->placements.clear();
            const int32_t x0 = chunkX * kSceneryChunkTiles;
            const int32_t y0 = chunkY * kSceneryChunkTiles;
            const int32_t x1 = std::min(x0 + kSceneryChunkTiles, _grid.tilesX());
            const int32_t y1 = std::min(y0 + kSceneryChunkTiles, _grid.tilesY());
            chunk->box.minX = static_cast<float>(x0);
            chunk->box.minY = static_cast<float>(y0);
            chunk->box.maxX = static_cast<float>(x1);
            chunk->box.maxY = static_cast<float>(y1);
            chunk->box.minZ = 0.0f;
            chunk->box.maxZ = 1.0f;
            for (int32_t y = y0; y < y1; y++)
            {
                for (int32_t x = x0; x < x1; x++)
                {
                    const TileCoordsXY tile{ x, y };
                    for (const SmallSceneryElement* element : TileElementsView<SmallSceneryElement>(tile))
                    {
                        if (element != nullptr)
                        {
                            appendElement(*chunk, tile, *element);
                        }
                    }
                }
            }
            if (!chunk->placements.empty())
            {
                chunk->box.minZ = chunk->placements[0].z;
                chunk->box.maxZ = chunk->placements[0].topZ;
                for (const ChunkPlacement& placement : chunk->placements)
                {
                    chunk->box.minZ = std::min(chunk->box.minZ, placement.z);
                    chunk->box.maxZ = std::max(chunk->box.maxZ, placement.topZ);
                }
            }
            chunk->dirty = false;
            chunk->built = true;
        }

        bool chunkVisible(const SceneryChunk& chunk, const FrameCull& cull)
        {
            if (!cull.haveCamera)
            {
                return true;
            }
            const float radius = sceneryCullRadius(cull.eyeX, cull.eyeY, cull.focusX, cull.focusY);
            return !boxOutsideGroundDistance(chunk.box, cull.focusX, cull.focusY, radius)
                && !boxOutsideFrustum(chunk.box, cull.planes);
        }

        void readCamera(FrameCull& cull)
        {
            float viewProj[16] = {};
            cull.haveCamera = copyCamera(cull.eyeX, cull.eyeY, cull.eyeZ, viewProj);
            if (!cull.haveCamera || !copyViewFocus(cull.focusX, cull.focusY))
            {
                cull.focusX = cull.eyeX;
                cull.focusY = cull.eyeY;
            }
            if (cull.haveCamera)
            {
                extractFrustumPlanes(viewProj, cull.planes);
            }
        }

        std::unordered_set<uint32_t> visibleModelKeys(const FrameCull& cull)
        {
            std::unordered_set<uint32_t> keys;
            for (int32_t y = 0; y < _grid.chunksY(); y++)
            {
                for (int32_t x = 0; x < _grid.chunksX(); x++)
                {
                    const SceneryChunk* chunk = _grid.at(x, y);
                    if (chunk == nullptr || !chunk->built || !chunkVisible(*chunk, cull))
                    {
                        continue;
                    }
                    for (const ChunkPlacement& placement : chunk->placements)
                    {
                        if (placement.modelCandidate)
                        {
                            keys.insert(placement.typeKey);
                        }
                    }
                }
            }
            return keys;
        }

        void syncChunks()
        {
            const TileCoordsXY mapSize = getGameState().mapSize;
            if (!_gridReady || _grid.tilesX() != mapSize.x || _grid.tilesY() != mapSize.y)
            {
                _grid.reset(mapSize.x, mapSize.y);
                _gridReady = true;
                _dirtyAll = true;
            }
            std::vector<std::pair<int32_t, int32_t>> tiles;
            bool all = false;
            {
                std::lock_guard<std::mutex> guard(_dirtyMutex);
                all = _dirtyAll;
                tiles.swap(_dirtyTiles);
                _dirtyAll = false;
            }
            if (all)
            {
                _grid.markAllDirty();
            }
            else
            {
                for (const auto& tile : tiles)
                {
                    _grid.markTile(tile.first, tile.second);
                }
            }
            int32_t rebuilt = 0;
            for (int32_t y = 0; y < _grid.chunksY() && rebuilt < kChunkRebuildsPerFrame; y++)
            {
                for (int32_t x = 0; x < _grid.chunksX() && rebuilt < kChunkRebuildsPerFrame; x++)
                {
                    if (_grid.dirty(x, y))
                    {
                        rebuildChunk(x, y);
                        rebuilt++;
                    }
                }
            }
            (void)rebuilt;
        }

        void uploadReadyModels(const FrameCull& cull)
        {
            ensureLoads();
            const std::unordered_set<uint32_t> pinned = visibleModelKeys(cull);
            uint32_t uploaded = 0;
            uint32_t bytes = 0;
            while (uploaded < kUploadsPerFrame && bytes < kUploadBytesPerFrame)
            {
                std::string identifier;
                std::optional<CpuMesh> mesh = _loads->takeReady(identifier);
                if (!mesh.has_value())
                {
                    break;
                }
                const uint32_t key = typeKeyFor(identifier);
                uint32_t meshBytes = 0;
                for (const MeshPrimitive& primitive : mesh->primitives)
                {
                    meshBytes += static_cast<uint32_t>(primitive.vertices.size() * sizeof(MeshVertex));
                    meshBytes += static_cast<uint32_t>(primitive.indices.size() * sizeof(uint16_t));
                }
                for (const MeshImage& image : mesh->images)
                {
                    if (!image.rgba.empty())
                    {
                        meshBytes += static_cast<uint32_t>(image.rgba.size());
                    }
                    else if (image.width > 0 && image.height > 0)
                    {
                        meshBytes += static_cast<uint32_t>(image.width) * static_cast<uint32_t>(image.height) * 4u;
                    }
                }
                auto releaseId = [&](uint32_t droppedId) {
                    const auto gpu = _gpu.find(droppedId);
                    if (gpu != _gpu.end())
                    {
                        if (gpu->second >= 0)
                        {
                            meshDrawReleaseModel(gpu->second);
                        }
                        _gpu.erase(gpu);
                    }
                    if (droppedId < _typeIds.size())
                    {
                        _loads->forget(_typeIds[droppedId]);
                    }
                };
                for (uint32_t droppedId : _vram.evict(meshBytes, pinned))
                {
                    releaseId(droppedId);
                }
                if (_vram.used() + meshBytes > _vram.budget())
                {
                    // Pinned set fills the budget. Do not upload past it, and do not
                    // remember a permanent failure: a later frame may have room.
                    _loads->forget(identifier);
                    continue;
                }
                int32_t handle = -1;
                bool canFit = true;
                for (const MeshPrimitive& primitive : mesh->primitives)
                {
                    if (primitive.vertices.size() > kSlabVertices || primitive.indices.size() > kSlabIndices)
                    {
                        canFit = false;
                    }
                }
                if (canFit)
                {
                    for (;;)
                    {
                        handle = meshDrawUploadModel(*mesh);
                        if (handle >= 0)
                        {
                            break;
                        }
                        const std::optional<uint32_t> dropped = _vram.evictOne(pinned);
                        if (!dropped.has_value())
                        {
                            break;
                        }
                        releaseId(*dropped);
                    }
                }
                if (handle >= 0)
                {
                    _gpu[key] = handle;
                    _vram.insert(key, meshBytes, _frame);
                    std::fprintf(
                        stderr, "View3D: loaded scenery model %s (%zu primitives)\n", identifier.c_str(),
                        mesh->primitives.size());
                }
                else
                {
                    _loads->forget(identifier);
                    if (_loggedUploadFail.insert(identifier).second)
                    {
                        std::fprintf(stderr, "View3D: model %s failed to upload\n", identifier.c_str());
                    }
                }
                uploaded++;
                bytes += meshBytes;
            }
        }

        void composeVisible(const FrameCull& cull)
        {
            _batches.clear();
            _modelBatches.clear();
            _spriteBatches.clear();
            std::vector<std::string> toLoad;
            for (int32_t y = 0; y < _grid.chunksY(); y++)
            {
                for (int32_t x = 0; x < _grid.chunksX(); x++)
                {
                    const SceneryChunk* chunk = _grid.at(x, y);
                    if (chunk == nullptr || !chunk->built)
                    {
                        continue;
                    }
                    if (!chunkVisible(*chunk, cull))
                    {
                        continue;
                    }
                    for (const ChunkPlacement& placement : chunk->placements)
                    {
                        int32_t modelHandle = -1;
                        if (placement.modelCandidate)
                        {
                            const auto gpu = _gpu.find(placement.typeKey);
                            if (gpu != _gpu.end() && gpu->second >= 0)
                            {
                                modelHandle = gpu->second;
                                _vram.touch(placement.typeKey, _frame);
                            }
                            else if (gpu == _gpu.end() && placement.typeKey < _typeIds.size())
                            {
                                toLoad.push_back(_typeIds[placement.typeKey]);
                                // Still loading. Do not upload the four sprite faces.
                                continue;
                            }
                        }
                        if (modelHandle >= 0)
                        {
                            const auto found = _modelBatches.find(placement.typeKey);
                            size_t index;
                            if (found == _modelBatches.end())
                            {
                                index = _batches.size();
                                _modelBatches.emplace(placement.typeKey, index);
                                Batch batch;
                                batch.handle = modelHandle;
                                batch.model = true;
                                batch.vegetation = placement.vegetation;
                                _batches.push_back(std::move(batch));
                            }
                            else
                            {
                                index = found->second;
                            }
                            _batches[index].instances.push_back(
                                MeshInstance{ placement.x, placement.y, placement.z, placement.yaw });
                            continue;
                        }
                        for (int face = 0; face < 4; face++)
                        {
                            if (!placement.spriteFace[face])
                            {
                                continue;
                            }
                            const int32_t handle = spriteHandleForFace(placement.spriteImage[face], face);
                            if (handle < 0)
                            {
                                continue;
                            }
                            const auto found = _spriteBatches.find(handle);
                            size_t index;
                            if (found == _spriteBatches.end())
                            {
                                index = _batches.size();
                                _spriteBatches.emplace(handle, index);
                                Batch batch;
                                batch.handle = handle;
                                batch.model = false;
                                batch.vegetation = placement.vegetation;
                                _batches.push_back(std::move(batch));
                            }
                            else
                            {
                                index = found->second;
                            }
                            _batches[index].instances.push_back(MeshInstance{ placement.x, placement.y, placement.z, 0.0f });
                        }
                    }
                }
            }
            if (!toLoad.empty())
            {
                ensureLoads();
                _loads->requestAbsent(toLoad);
            }
        }

        bool hideBatch(const Batch& batch)
        {
            const WindowBase* window = WindowGetMain();
            const Viewport* viewport = window != nullptr ? window->viewport : nullptr;
            if (viewport == nullptr)
            {
                return false;
            }
            if (batch.vegetation)
            {
                return viewport->flags.has(ViewportFlag::hideVegetation);
            }
            return viewport->flags.has(ViewportFlag::hideScenery);
        }
    } // namespace

    void releaseScenery()
    {
        _loads.reset();
        meshDrawShutdown();
        _spriteHandles.clear();
        _loggedUploadFail.clear();
        _batches.clear();
        _modelBatches.clear();
        _spriteBatches.clear();
        _loggedShader = false;
        _blit.clear();
        _rgba.clear();
        _grid = {};
        _gridReady = false;
        _typeIds.clear();
        _typeIndex.clear();
        _gpu.clear();
        _vram = LruBudget(kDefaultVramBudgetBytes);
        _frame = 0;
        std::lock_guard<std::mutex> guard(_dirtyMutex);
        _dirtyTiles.clear();
        _dirtyAll = true;
    }

    void drawScenery()
    {
        if (!meshDrawInit())
        {
            if (!_loggedShader)
            {
                _loggedShader = true;
                std::fprintf(stderr, "View3D: scenery shader failed\n");
            }
            return;
        }
        _frame++;
        if (_dropSprites)
        {
            _dropSprites = false;
            _spriteHandles.clear();
            meshDrawClearSprites();
        }
        syncChunks();
        FrameCull cull;
        readCamera(cull);
        uploadReadyModels(cull);
        composeVisible(cull);
        for (const Batch& batch : _batches)
        {
            if (batch.instances.empty() || hideBatch(batch))
            {
                continue;
            }
            if (batch.model)
            {
                meshDrawModel(batch.handle, batch.instances.data(), static_cast<uint32_t>(batch.instances.size()));
            }
            else
            {
                meshDrawSprite(batch.handle, batch.instances.data(), static_cast<uint32_t>(batch.instances.size()));
            }
        }
    }
} // namespace OpenRCT2::View3D::Park
