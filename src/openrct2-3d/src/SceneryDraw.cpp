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

        struct SpriteCache
        {
            ImageId image;
            int face = 0;
            int32_t handle = -1;
        };

        std::vector<SpriteCache> _sprites;
        std::vector<Batch> _batches;
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
            if (!image.IsRemap())
            {
                if (entry.flags.has(SmallSceneryFlag::hasPrimaryColour))
                {
                    image = image.WithPrimary(element.getPrimaryColour());
                    if (entry.flags.has(SmallSceneryFlag::hasSecondaryColour))
                    {
                        image = image.WithSecondary(element.getSecondaryColour());
                    }
                }
                if (entry.flags.has(SmallSceneryFlag::hasTertiaryColour))
                {
                    image = image.WithTertiary(element.getTertiaryColour());
                }
            }
            return image;
        }

        const Drawing::GamePalette& texturePalette()
        {
            return Drawing::LightFx::IsAvailable() ? Drawing::LightFx::GetPalette() : Drawing::gPalette;
        }

        int32_t spriteHandleForFace(const ImageId& image, int face)
        {
            for (const SpriteCache& cached : _sprites)
            {
                if (cached.face == face && cached.image == image)
                {
                    return cached.handle;
                }
            }
            const G1Element* g1 = GfxGetG1Element(image);
            if (g1 == nullptr || g1->width <= 0 || g1->height <= 0 || g1->width > kMaxSpriteEdge || g1->height > kMaxSpriteEdge)
            {
                _sprites.push_back(SpriteCache{ image, face, -1 });
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
            _sprites.push_back(SpriteCache{ image, face, handle });
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
            for (int face = 0; face < 4; face++)
            {
                placement.spriteHandles[face] = spriteHandleForFace(faceImage(element, *entry, face), face);
            }
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
            chunk->box.maxZ = 8.0f;
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
            chunk->dirty = false;
            chunk->built = true;
        }

        bool chunkVisible(
            const SceneryChunk& chunk, bool haveCamera, float eyeX, float eyeY, float eyeZ, const float planes[6][4])
        {
            if (!haveCamera)
            {
                return true;
            }
            return !boxOutsideDistance(chunk.box, eyeX, eyeY, eyeZ, kDefaultViewDistanceTiles)
                && !boxOutsideFrustum(chunk.box, planes);
        }

        void readCamera(bool& haveCamera, float& eyeX, float& eyeY, float& eyeZ, float planes[6][4])
        {
            float viewProj[16] = {};
            haveCamera = copyCamera(eyeX, eyeY, eyeZ, viewProj);
            if (haveCamera)
            {
                extractFrustumPlanes(viewProj, planes);
            }
        }

        std::vector<uint32_t> visibleModelKeys()
        {
            std::vector<uint32_t> keys;
            bool haveCamera = false;
            float eyeX = 0.0f;
            float eyeY = 0.0f;
            float eyeZ = 0.0f;
            float planes[6][4] = {};
            readCamera(haveCamera, eyeX, eyeY, eyeZ, planes);
            for (int32_t y = 0; y < _grid.chunksY(); y++)
            {
                for (int32_t x = 0; x < _grid.chunksX(); x++)
                {
                    const SceneryChunk* chunk = _grid.at(x, y);
                    if (chunk == nullptr || !chunk->built || !chunkVisible(*chunk, haveCamera, eyeX, eyeY, eyeZ, planes))
                    {
                        continue;
                    }
                    for (const ChunkPlacement& placement : chunk->placements)
                    {
                        if (placement.modelCandidate)
                        {
                            keys.push_back(placement.typeKey);
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
            for (int32_t y = 0; y < _grid.chunksY(); y++)
            {
                for (int32_t x = 0; x < _grid.chunksX(); x++)
                {
                    if (_grid.dirty(x, y))
                    {
                        rebuildChunk(x, y);
                        rebuilt++;
                    }
                }
            }
            if (rebuilt > 0)
            {
                std::fprintf(stderr, "View3D: rebuilt %d scenery chunk(s)\n", rebuilt);
            }
        }

        void uploadReadyModels()
        {
            ensureLoads();
            const std::vector<uint32_t> pinned = visibleModelKeys();
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
                    meshBytes += static_cast<uint32_t>(primitive.indices.size() * sizeof(uint32_t));
                }
                for (const MeshImage& image : mesh->images)
                {
                    meshBytes += static_cast<uint32_t>(image.rgba.size() + image.png.size());
                }
                const std::vector<uint32_t> dropped = _vram.evict(meshBytes, pinned);
                for (uint32_t droppedId : dropped)
                {
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
                }
                const int32_t handle = meshDrawUploadModel(*mesh);
                _gpu[key] = handle;
                if (handle >= 0)
                {
                    _vram.insert(key, meshBytes, _frame);
                    std::fprintf(
                        stderr, "View3D: loaded scenery model %s (%zu primitives)\n", identifier.c_str(),
                        mesh->primitives.size());
                }
                else
                {
                    std::fprintf(stderr, "View3D: model %s failed to upload\n", identifier.c_str());
                }
                uploaded++;
                bytes += meshBytes;
            }
        }

        void composeVisible()
        {
            _batches.clear();
            bool haveCamera = false;
            float eyeX = 0.0f;
            float eyeY = 0.0f;
            float eyeZ = 0.0f;
            float planes[6][4] = {};
            readCamera(haveCamera, eyeX, eyeY, eyeZ, planes);
            std::unordered_map<uint32_t, size_t> modelBatches;
            std::unordered_map<int32_t, size_t> spriteBatches;
            for (int32_t y = 0; y < _grid.chunksY(); y++)
            {
                for (int32_t x = 0; x < _grid.chunksX(); x++)
                {
                    const SceneryChunk* chunk = _grid.at(x, y);
                    if (chunk == nullptr || !chunk->built)
                    {
                        continue;
                    }
                    if (!chunkVisible(*chunk, haveCamera, eyeX, eyeY, eyeZ, planes))
                    {
                        continue;
                    }
                    for (const ChunkPlacement& placement : chunk->placements)
                    {
                        int32_t modelHandle = -1;
                        if (placement.modelCandidate)
                        {
                            const auto gpu = _gpu.find(placement.typeKey);
                            if (gpu != _gpu.end())
                            {
                                modelHandle = gpu->second;
                            }
                            else if (placement.typeKey < _typeIds.size())
                            {
                                ensureLoads();
                                const std::string& identifier = _typeIds[placement.typeKey];
                                if (_loads->phase(identifier) == ModelLoadPhase::absent)
                                {
                                    _loads->request(identifier);
                                }
                            }
                            if (modelHandle >= 0)
                            {
                                _vram.touch(placement.typeKey, _frame);
                            }
                        }
                        if (modelHandle >= 0)
                        {
                            const auto found = modelBatches.find(placement.typeKey);
                            size_t index;
                            if (found == modelBatches.end())
                            {
                                index = _batches.size();
                                modelBatches.emplace(placement.typeKey, index);
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
                            const int32_t handle = placement.spriteHandles[face];
                            if (handle < 0)
                            {
                                continue;
                            }
                            const auto found = spriteBatches.find(handle);
                            size_t index;
                            if (found == spriteBatches.end())
                            {
                                index = _batches.size();
                                spriteBatches.emplace(handle, index);
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
        _sprites.clear();
        _batches.clear();
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
        syncChunks();
        uploadReadyModels();
        composeVisible();
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
