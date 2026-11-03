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
#include <bgfx/bgfx.h>
#include <cstdio>
#include <cstring>
#include <list>
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
        constexpr int32_t kGpuPermanent = -2;
        constexpr int32_t kGpuNeedsSpace = -3;

        struct Batch
        {
            int32_t handle = -1;
            bool model = false;
            bool vegetation = false;
            bool distant = false;
            int32_t page = -1;
            int face = 0;
            std::vector<MeshInstance> instances;
            std::vector<DistantSpriteInstance> distantInstances;
        };

        struct SpriteResident
        {
            int32_t handle = -1;
            int32_t page = -1;
            uint32_t texels = 0;
            uint64_t lastUsed = 0;
            bool dedicated = false;
            float uvScaleX = 1.0f;
            float uvScaleY = 1.0f;
            float uvOffX = 0.0f;
            float uvOffY = 0.0f;
            float left = 0.0f;
            float right = 0.0f;
            float bottom = 0.0f;
            float top = 0.0f;
        };

        struct SpriteKey
        {
            ImageId image;
            int face = 0;
            bool operator==(const SpriteKey& other) const
            {
                return face == other.face && image == other.image;
            }
        };

        struct SpriteKeyHash
        {
            size_t operator()(const SpriteKey& key) const
            {
                size_t hash = std::hash<uint32_t>{}(static_cast<uint32_t>(key.image.GetIndex()));
                hash ^= static_cast<size_t>(static_cast<uint8_t>(key.image.GetPrimary())) << 8;
                hash ^= static_cast<size_t>(static_cast<uint8_t>(key.image.GetSecondary())) << 16;
                hash ^= static_cast<size_t>(static_cast<uint8_t>(key.image.GetTertiary())) << 24;
                hash ^= static_cast<size_t>(key.face) << 1;
                if (key.image.IsRemap())
                {
                    hash ^= 0x9e3779b97f4a7c15uLL;
                }
                return hash;
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
            // 3D radius. Far side capped at the eye-to-focus distance.
            float radius = kDefaultViewDistanceTiles;
            // Uncapped ground footprint. The sprite layer is clipped to the map and the far limit.
            float reach = kDefaultViewDistanceTiles;
            float spriteRadius = kDefaultViewDistanceTiles;
            float planes[6][4] = {};
            float viewProj[16] = {};
        };

        bool _dropSprites = false;

        std::unordered_map<SpriteKey, SpriteResident, SpriteKeyHash> _spriteResidents;
        // Near faces this frame. Eviction will not drop these to admit a distant sprite.
        std::unordered_set<SpriteKey, SpriteKeyHash> _nearFaces;
        std::unordered_set<std::string> _loggedUploadFail;
        std::vector<Batch> _batches;
        size_t _batchCount = 0;
        std::unordered_map<uint32_t, size_t> _modelBatches;
        std::unordered_map<int32_t, size_t> _spriteBatches;
        std::unordered_map<int32_t, size_t> _distantPages;
        bool _loggedShader = false;
        std::vector<Drawing::PaletteIndex> _blit;
        std::vector<uint8_t> _rgba;
        SceneryChunkGrid _grid;
        bool _gridReady = false;
        std::vector<std::string> _typeIds;
        std::unordered_map<std::string, uint32_t> _typeIndex;
        std::unordered_map<uint32_t, int32_t> _gpu;
        std::unordered_map<uint32_t, CpuMesh> _held;
        std::unordered_map<uint32_t, uint64_t> _retryAt;
        std::unordered_map<uint32_t, uint32_t> _backoff;
        // Last held key that took a retry slot. The next frame starts after it.
        uint32_t _heldCursor = 0;
        bool _spaceFreed = false;
        std::unordered_set<uint32_t> _pinned;
        std::unordered_set<uint32_t> _loadKeys;
        int32_t _spriteUploads = 0;
        uint32_t _spriteCount = 0;
        uint32_t _spriteTexels = 0;
        uint32_t _spriteDedicated = 0;
        std::list<SpriteKey> _spriteLru;
        std::unordered_map<SpriteKey, std::list<SpriteKey>::iterator, SpriteKeyHash> _spriteLruPos;
        // Upload failed after eviction. Draw the X-sprite and try the model again.
        // This is not _gpu[key] = -1: a later frame can still admit the mesh.
        std::unique_ptr<ModelLoadQueue> _loads;
        std::mutex _dirtyMutex;
        std::vector<std::pair<int32_t, int32_t>> _dirtyTiles;
        bool _dirtyAll = true;
        uint64_t _frame = 0;
        LruBudget _vram(kDefaultVramBudgetBytes);
        SceneryFrameStats _stats;
        ScenerySpriteOverride _spriteOverride = nullptr;

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

        void touchSprite(const SpriteKey& key)
        {
            const auto found = _spriteLruPos.find(key);
            if (found == _spriteLruPos.end())
            {
                _spriteLru.push_front(key);
                _spriteLruPos.emplace(key, _spriteLru.begin());
                return;
            }
            _spriteLru.splice(_spriteLru.begin(), _spriteLru, found->second);
        }

        void forgetSpriteKey(const SpriteKey& key)
        {
            const auto listed = _spriteLruPos.find(key);
            if (listed == _spriteLruPos.end())
            {
                return;
            }
            _spriteLru.erase(listed->second);
            _spriteLruPos.erase(listed);
        }

        void releaseSprite(const SpriteKey& key)
        {
            const auto found = _spriteResidents.find(key);
            if (found == _spriteResidents.end())
            {
                forgetSpriteKey(key);
                return;
            }
            if (found->second.handle >= 0)
            {
                meshDrawReleaseSprite(found->second.handle);
                if (found->second.dedicated)
                {
                    if (_spriteDedicated > 0)
                    {
                        _spriteDedicated--;
                    }
                }
                else if (_spriteTexels >= found->second.texels)
                {
                    _spriteTexels -= found->second.texels;
                }
                if (_spriteCount > 0)
                {
                    _spriteCount--;
                }
                _spaceFreed = true;
            }
            _spriteResidents.erase(found);
            forgetSpriteKey(key);
        }

        bool evictColdSprite(bool requireStale, bool protectNear = false)
        {
            std::vector<SpriteKey> keys;
            std::vector<uint64_t> lastUsed;
            std::vector<uint8_t> used;
            keys.reserve(_spriteLru.size());
            for (const SpriteKey& key : _spriteLru)
            {
                const auto found = _spriteResidents.find(key);
                if (found == _spriteResidents.end() || found->second.handle < 0)
                {
                    continue;
                }
                if (protectNear && _nearFaces.find(key) != _nearFaces.end())
                {
                    continue;
                }
                keys.push_back(key);
                lastUsed.push_back(found->second.lastUsed);
                used.push_back(static_cast<uint8_t>(found->second.lastUsed == _frame ? 1 : 0));
            }
            const int32_t index = pickSpriteToDrop(
                lastUsed.data(), used.data(), static_cast<int32_t>(keys.size()), _frame, kSpriteStaleFrames, requireStale);
            if (index < 0)
            {
                return false;
            }
            releaseSprite(keys[static_cast<size_t>(index)]);
            return true;
        }

        void dropStaleSprites()
        {
            while (evictColdSprite(true))
            {
            }
        }

        // `near` is an X-sprite inside the 3D radius. It is not refused by the image cap or the
        // per-frame upload budget, and it can take a slot from a sprite only the distant layer uses.
        int32_t spriteHandleForFace(const ImageId& image, int face, bool near)
        {
            const SpriteKey key{ image, face };
            const auto found = _spriteResidents.find(key);
            if (found != _spriteResidents.end())
            {
                if (found->second.handle >= 0)
                {
                    found->second.lastUsed = _frame;
                    touchSprite(key);
                }
                return found->second.handle;
            }
            if (!near && _spriteUploads >= static_cast<int32_t>(kSpriteUploadsPerFrame))
            {
                return -1;
            }
            const G1Element* g1 = GfxGetG1Element(image);
            int32_t overrideWidth = 0;
            int32_t overrideHeight = 0;
            const uint8_t* overrideRgba = nullptr;
            const bool useOverride = (g1 == nullptr || g1->width <= 0 || g1->height <= 0 || g1->width > kMaxSpriteEdge
                                      || g1->height > kMaxSpriteEdge)
                && _spriteOverride != nullptr && _spriteOverride(overrideWidth, overrideHeight, overrideRgba)
                && overrideRgba != nullptr && overrideWidth > 0 && overrideHeight > 0 && overrideWidth <= kMaxSpriteEdge
                && overrideHeight <= kMaxSpriteEdge;
            if (!useOverride
                && (g1 == nullptr || g1->width <= 0 || g1->height <= 0 || g1->width > kMaxSpriteEdge
                    || g1->height > kMaxSpriteEdge))
            {
                SpriteResident failed;
                failed.lastUsed = _frame;
                _spriteResidents.emplace(key, failed);
                return -1;
            }
            const int32_t width = useOverride ? overrideWidth : g1->width;
            const int32_t height = useOverride ? overrideHeight : g1->height;
            const uint32_t texels = static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
            const auto room = [&](bool dedicated) {
                // A count of 0 skips the image cap. Near faces may pass kMaxSpriteImages.
                const uint32_t count = near ? 0u : _spriteCount + 1u;
                return !spriteOverShare(
                    count, kMaxSpriteImages, _spriteTexels + (dedicated ? 0u : texels), kMaxSpriteAtlasTexels,
                    _spriteDedicated + (dedicated ? 1u : 0u), kMaxSpriteDedicated);
            };
            if (near)
            {
                while (_spriteCount >= kMaxSpriteImages && evictColdSprite(false, true))
                {
                }
            }
            while (!room(false) && evictColdSprite(false, true))
            {
            }
            if (!room(false))
            {
                return -1;
            }
            const uint8_t* rgba = nullptr;
            int16_t xOffset = 0;
            int16_t yOffset = 0;
            if (useOverride)
            {
                rgba = overrideRgba;
            }
            else
            {
                xOffset = g1->xOffset;
                yOffset = g1->yOffset;
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
                rgba = _rgba.data();
            }
            const SpriteQuad quad = makeDirectionSpriteQuad(face, width, height, xOffset, yOffset);
            const bool allowDedicated = _spriteDedicated < kMaxSpriteDedicated;
            SpritePlace place = meshDrawUploadSprite(quad, rgba, width, height, allowDedicated);
            if (place.id < 0 && evictColdSprite(false, true))
            {
                place = meshDrawUploadSprite(quad, rgba, width, height, _spriteDedicated < kMaxSpriteDedicated);
            }
            if (place.id < 0)
            {
                return -1;
            }
            if (place.page < 0 && !room(true))
            {
                meshDrawReleaseSprite(place.id);
                return -1;
            }
            SpriteResident resident;
            resident.handle = place.id;
            resident.page = place.page;
            resident.texels = place.page >= 0 ? texels : 0u;
            resident.lastUsed = _frame;
            resident.dedicated = place.page < 0;
            resident.uvScaleX = place.uvScaleX;
            resident.uvScaleY = place.uvScaleY;
            resident.uvOffX = place.uvOffX;
            resident.uvOffY = place.uvOffY;
            resident.left = static_cast<float>(xOffset) * kSpritePixelAcross;
            resident.right = static_cast<float>(xOffset + width) * kSpritePixelAcross;
            resident.top = -static_cast<float>(yOffset) * kViewerZPerSimUnit;
            resident.bottom = -static_cast<float>(yOffset + height) * kViewerZPerSimUnit;
            _spriteCount++;
            if (resident.dedicated)
            {
                _spriteDedicated++;
            }
            else
            {
                _spriteTexels += resident.texels;
            }
            if (!near)
            {
                _spriteUploads++;
            }
            _spriteResidents.emplace(key, resident);
            touchSprite(key);
            return place.id;
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
                placement.spriteImage[face] = faceImage(element, *entry, face);
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
            buildChunkSpriteRuns(*chunk);
        }

        void beginFrameBatches()
        {
            _modelBatches.clear();
            _spriteBatches.clear();
            _distantPages.clear();
            _batchCount = 0;
        }

        size_t takeBatch()
        {
            if (_batchCount >= _batches.size())
            {
                _batches.emplace_back();
            }
            Batch& batch = _batches[_batchCount];
            batch.instances.clear();
            batch.distantInstances.clear();
            batch.handle = -1;
            batch.model = false;
            batch.vegetation = false;
            batch.distant = false;
            batch.page = -1;
            batch.face = 0;
            return _batchCount++;
        }

        void capSpriteRadius(FrameCull& cull)
        {
            float limit = sceneryDistantSpriteTiles();
            if (cull.haveCamera)
            {
                const float mapFar = mapCornerDistance(
                    cull.focusX, cull.focusY, static_cast<float>(_grid.tilesX()), static_cast<float>(_grid.tilesY()));
                limit = std::min(limit, mapFar);
            }
            cull.spriteRadius = std::min(cull.reach, limit);
        }

        void appendDistant(const SceneryChunkGrid& grid, const FrameCull& cull, int face)
        {
            appendDistantPlacements(
                grid, cull.planes, cull.focusX, cull.focusY, cull.radius, cull.spriteRadius, face,
                [&](const SceneryChunk& chunk, const ChunkSpriteRun& run) {
                    const int32_t handle = spriteHandleForFace(run.image, face & 3, false);
                    if (handle < 0)
                    {
                        return;
                    }
                    const auto resident = _spriteResidents.find(SpriteKey{ run.image, face & 3 });
                    if (resident == _spriteResidents.end() || resident->second.handle < 0)
                    {
                        return;
                    }
                    if (resident->second.page < 0)
                    {
                        const int32_t dedicatedHandle = resident->second.handle;
                        const auto spriteBatch = _spriteBatches.find(dedicatedHandle);
                        size_t index;
                        if (spriteBatch == _spriteBatches.end())
                        {
                            index = takeBatch();
                            _spriteBatches.emplace(dedicatedHandle, index);
                            _batches[index].handle = dedicatedHandle;
                            _batches[index].vegetation = run.vegetation;
                        }
                        else
                        {
                            index = spriteBatch->second;
                            if (!run.vegetation)
                            {
                                _batches[index].vegetation = false;
                            }
                        }
                        Batch& fallback = _batches[index];
                        fallback.instances.reserve(fallback.instances.size() + static_cast<size_t>(run.count));
                        const std::vector<uint32_t>& order = chunk.spriteOrder[face & 3];
                        for (uint32_t n = 0; n < run.count; n++)
                        {
                            const ChunkSpritePoint& point = chunk.spriteInstances[order[run.begin + n]];
                            fallback.instances.push_back(MeshInstance{ point.x, point.y, point.z, 0.0f });
                        }
                        return;
                    }
                    const int32_t page = resident->second.page;
                    const auto found = _distantPages.find(page);
                    size_t batchIndex;
                    if (found == _distantPages.end())
                    {
                        batchIndex = takeBatch();
                        _distantPages.emplace(page, batchIndex);
                        _batches[batchIndex].distant = true;
                        _batches[batchIndex].page = page;
                        _batches[batchIndex].face = face & 3;
                        _batches[batchIndex].vegetation = run.vegetation;
                    }
                    else
                    {
                        batchIndex = found->second;
                        if (!run.vegetation)
                        {
                            _batches[batchIndex].vegetation = false;
                        }
                    }
                    Batch& batch = _batches[batchIndex];
                    batch.distantInstances.reserve(batch.distantInstances.size() + static_cast<size_t>(run.count));
                    const std::vector<uint32_t>& order = chunk.spriteOrder[face & 3];
                    const SpriteResident& sprite = resident->second;
                    for (uint32_t n = 0; n < run.count; n++)
                    {
                        const ChunkSpritePoint& point = chunk.spriteInstances[order[run.begin + n]];
                        DistantSpriteInstance instance;
                        instance.x = point.x;
                        instance.y = point.y;
                        instance.z = point.z;
                        instance.left = sprite.left;
                        instance.right = sprite.right;
                        instance.bottom = sprite.bottom;
                        instance.top = sprite.top;
                        instance.uvScaleX = sprite.uvScaleX;
                        instance.uvScaleY = sprite.uvScaleY;
                        instance.uvOffX = sprite.uvOffX;
                        instance.uvOffY = sprite.uvOffY;
                        batch.distantInstances.push_back(instance);
                    }
                });
        }

        bool chunkVisible(const SceneryChunk& chunk, const FrameCull& cull)
        {
            if (!cull.haveCamera)
            {
                return true;
            }
            return !boxOutsideGroundDistance(chunk.box, cull.focusX, cull.focusY, cull.radius)
                && !boxOutsideFrustum(chunk.box, cull.planes);
        }

        void readCamera(FrameCull& cull)
        {
            cull.haveCamera = copyCamera(cull.eyeX, cull.eyeY, cull.eyeZ, cull.viewProj);
            if (!cull.haveCamera || !copyViewFocus(cull.focusX, cull.focusY))
            {
                cull.focusX = cull.eyeX;
                cull.focusY = cull.eyeY;
            }
            if (cull.haveCamera)
            {
                extractFrustumPlanes(cull.viewProj, cull.planes);
                cull.radius = groundFootprintRadius(cull.viewProj, cull.eyeX, cull.eyeY, cull.focusX, cull.focusY);
                cull.reach = groundReachRadius(cull.viewProj, cull.focusX, cull.focusY);
            }
        }

        void syncChunks(float focusX, float focusY)
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
            struct DirtyChunk
            {
                int32_t x = 0;
                int32_t y = 0;
                float distance2 = 0.0f;
            };
            std::vector<DirtyChunk> dirty;
            for (int32_t y = 0; y < _grid.chunksY(); y++)
            {
                for (int32_t x = 0; x < _grid.chunksX(); x++)
                {
                    if (!_grid.dirty(x, y))
                    {
                        continue;
                    }
                    const float cx = (static_cast<float>(x) + 0.5f) * static_cast<float>(kSceneryChunkTiles);
                    const float cy = (static_cast<float>(y) + 0.5f) * static_cast<float>(kSceneryChunkTiles);
                    const float dx = cx - focusX;
                    const float dy = cy - focusY;
                    dirty.push_back(DirtyChunk{ x, y, dx * dx + dy * dy });
                }
            }
            std::sort(
                dirty.begin(), dirty.end(), [](const DirtyChunk& a, const DirtyChunk& b) { return a.distance2 < b.distance2; });
            const size_t rebuilds = std::min(dirty.size(), static_cast<size_t>(kChunkRebuildsPerFrame));
            for (size_t i = 0; i < rebuilds; i++)
            {
                rebuildChunk(dirty[i].x, dirty[i].y);
            }
        }

        uint32_t meshBytesOf(const CpuMesh& mesh)
        {
            uint32_t meshBytes = 0;
            for (const MeshPrimitive& primitive : mesh.primitives)
            {
                meshBytes += static_cast<uint32_t>(primitive.vertices.size() * sizeof(MeshVertex));
                meshBytes += static_cast<uint32_t>(primitive.indices.size() * sizeof(uint16_t));
            }
            for (const MeshImage& image : mesh.images)
            {
                if (!image.rgba.empty())
                {
                    meshBytes += static_cast<uint32_t>(image.rgba.size());
                }
            }
            return meshBytes;
        }

        void releaseResident(uint32_t droppedId)
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
            _held.erase(droppedId);
            _retryAt.erase(droppedId);
            _backoff.erase(droppedId);
            if (droppedId < _typeIds.size())
            {
                _loads->forget(_typeIds[droppedId]);
            }
            _spaceFreed = true;
        }

        void notePermanent(uint32_t key, const std::string& identifier)
        {
            _gpu[key] = kGpuPermanent;
            _held.erase(key);
            _retryAt.erase(key);
            _backoff.erase(key);
            if (_loggedUploadFail.insert(identifier).second)
            {
                std::fprintf(stderr, "View3D: model %s failed to upload\n", identifier.c_str());
            }
        }

        void noteSpace(uint32_t key, CpuMesh mesh)
        {
            _held[key] = std::move(mesh);
            _gpu[key] = kGpuNeedsSpace;
            uint32_t delay = 8;
            const auto found = _backoff.find(key);
            if (found != _backoff.end())
            {
                delay = std::min(64u, found->second * 2u);
            }
            _backoff[key] = delay;
            _retryAt[key] = _frame + delay;
        }

        bool uploadOne(uint32_t key, const std::string& identifier, CpuMesh mesh, bool allowEvict)
        {
            const uint32_t meshBytes = meshBytesOf(mesh);
            if (_vram.used() + meshBytes > _vram.budget())
            {
                if (allowEvict)
                {
                    for (uint32_t droppedId : _vram.evict(meshBytes, _pinned))
                    {
                        releaseResident(droppedId);
                    }
                }
                if (_vram.used() + meshBytes > _vram.budget())
                {
                    noteSpace(key, std::move(mesh));
                    return true;
                }
            }
            bool geometryFits = true;
            for (const MeshPrimitive& primitive : mesh.primitives)
            {
                if (primitive.vertices.size() > kSlabVertices || primitive.indices.size() > kSlabIndices)
                {
                    geometryFits = false;
                }
            }
            MeshUpload uploaded{ MeshUploadStatus::rejected, -1 };
            if (geometryFits)
            {
                uploaded = meshDrawUploadModel(mesh);
                if (uploaded.status == MeshUploadStatus::capacity && allowEvict)
                {
                    const std::optional<uint32_t> dropped = _vram.evictOne(_pinned);
                    if (dropped.has_value())
                    {
                        releaseResident(*dropped);
                        uploaded = meshDrawUploadModel(mesh);
                    }
                    // Models inside the radius outrank sprites. Free a cold sprite and retry.
                    for (int32_t attempt = 0; attempt < 8 && uploaded.status == MeshUploadStatus::capacity; attempt++)
                    {
                        if (!evictColdSprite(false))
                        {
                            break;
                        }
                        uploaded = meshDrawUploadModel(mesh);
                    }
                }
            }
            if (uploaded.status == MeshUploadStatus::ready)
            {
                _gpu[key] = uploaded.id;
                _vram.insert(key, meshBytes, _frame);
                _held.erase(key);
                _retryAt.erase(key);
                _backoff.erase(key);
                return false;
            }
            if (uploaded.status == MeshUploadStatus::rejected)
            {
                notePermanent(key, identifier);
                _loads->forget(identifier);
                return false;
            }
            noteSpace(key, std::move(mesh));
            return true;
        }

        void uploadReadyModels()
        {
            ensureLoads();
            for (;;)
            {
                const std::optional<std::string> failed = _loads->takeFailed();
                if (!failed.has_value())
                {
                    break;
                }
                notePermanent(typeKeyFor(*failed), *failed);
            }
            uint32_t uploaded = 0;
            uint32_t bytes = 0;
            {
                std::vector<uint32_t> keys;
                keys.reserve(_held.size());
                for (const auto& item : _held)
                {
                    keys.push_back(item.first);
                }
                std::sort(keys.begin(), keys.end());
                std::vector<uint32_t> pinnedKeys;
                std::vector<uint8_t> due;
                pinnedKeys.reserve(keys.size());
                due.reserve(keys.size());
                for (uint32_t key : keys)
                {
                    if (_pinned.find(key) == _pinned.end())
                    {
                        _held.erase(key);
                        _gpu.erase(key);
                        _retryAt.erase(key);
                        _backoff.erase(key);
                        if (key < _typeIds.size())
                        {
                            _loads->forget(_typeIds[key]);
                        }
                        continue;
                    }
                    const auto when = _retryAt.find(key);
                    const bool isDue = when == _retryAt.end() || _frame >= when->second;
                    pinnedKeys.push_back(key);
                    due.push_back(static_cast<uint8_t>(isDue ? 1 : 0));
                }
                // One retry. The remaining slots stay free for models that just finished loading.
                if (!pinnedKeys.empty() && uploaded < kUploadsPerFrame && bytes < kUploadBytesPerFrame)
                {
                    const int32_t index = selectHeldRetry(
                        pinnedKeys.data(), due.data(), static_cast<int32_t>(pinnedKeys.size()), _spaceFreed, _heldCursor);
                    if (index >= 0)
                    {
                        const uint32_t key = pinnedKeys[static_cast<size_t>(index)];
                        const auto held = _held.find(key);
                        if (held != _held.end())
                        {
                            CpuMesh mesh = std::move(held->second);
                            _held.erase(held);
                            const std::string identifier = key < _typeIds.size() ? _typeIds[key] : std::string();
                            const uint32_t meshBytes = meshBytesOf(mesh);
                            uploadOne(key, identifier, std::move(mesh), true);
                            uploaded++;
                            bytes += meshBytes;
                        }
                    }
                }
                _spaceFreed = false;
            }
            while (uploaded < kUploadsPerFrame && bytes < kUploadBytesPerFrame)
            {
                std::string identifier;
                std::optional<CpuMesh> mesh = _loads->takeReady(identifier);
                if (!mesh.has_value())
                {
                    break;
                }
                const uint32_t key = typeKeyFor(identifier);
                const uint32_t meshBytes = meshBytesOf(*mesh);
                const bool stop = uploadOne(key, identifier, std::move(*mesh), true);
                uploaded++;
                bytes += meshBytes;
                if (stop)
                {
                    break;
                }
            }
        }

        void markNearFaces(const FrameCull& cull)
        {
            _nearFaces.clear();
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
                        bool resident = false;
                        if (placement.modelCandidate)
                        {
                            const auto gpu = _gpu.find(placement.typeKey);
                            resident = gpu != _gpu.end() && gpu->second >= 0;
                        }
                        if (placementDraw(placement.modelCandidate, resident, false) != PlacementDraw::sprite)
                        {
                            continue;
                        }
                        for (int face = 0; face < 4; face++)
                        {
                            if (!placement.spriteFace[face])
                            {
                                continue;
                            }
                            _nearFaces.insert(SpriteKey{ placement.spriteImage[face], face });
                        }
                    }
                }
            }
        }

        void composeVisible(const FrameCull& cull)
        {
            beginFrameBatches();
            // Near faces first. The distant layer then keeps only the slots they left.
            markNearFaces(cull);
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
                        bool resident = false;
                        int32_t modelHandle = -1;
                        if (placement.modelCandidate)
                        {
                            const auto gpu = _gpu.find(placement.typeKey);
                            if (gpu != _gpu.end() && gpu->second >= 0)
                            {
                                resident = true;
                                modelHandle = gpu->second;
                                _vram.touch(placement.typeKey, _frame);
                            }
                            else
                            {
                                _pinned.insert(placement.typeKey);
                                const bool blocked = gpu != _gpu.end()
                                    && (gpu->second == kGpuPermanent || gpu->second == kGpuNeedsSpace);
                                if (!blocked && _held.find(placement.typeKey) == _held.end()
                                    && placement.typeKey < _typeIds.size())
                                {
                                    _loadKeys.insert(placement.typeKey);
                                }
                            }
                        }
                        if (resident)
                        {
                            _pinned.insert(placement.typeKey);
                        }
                        const PlacementDraw kind = placementDraw(placement.modelCandidate, resident, false);
                        if (kind == PlacementDraw::model)
                        {
                            const auto found = _modelBatches.find(placement.typeKey);
                            size_t index;
                            if (found == _modelBatches.end())
                            {
                                index = takeBatch();
                                _modelBatches.emplace(placement.typeKey, index);
                                _batches[index].handle = modelHandle;
                                _batches[index].model = true;
                                _batches[index].vegetation = placement.vegetation;
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
                            const int32_t handle = spriteHandleForFace(placement.spriteImage[face], face, true);
                            if (handle < 0)
                            {
                                continue;
                            }
                            const auto found = _spriteBatches.find(handle);
                            size_t index;
                            if (found == _spriteBatches.end())
                            {
                                index = takeBatch();
                                _spriteBatches.emplace(handle, index);
                                _batches[index].handle = handle;
                                _batches[index].model = false;
                                _batches[index].vegetation = placement.vegetation;
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
            if (cull.haveCamera)
            {
                const int distantFace = spriteFaceForView(cull.eyeX - cull.focusX, cull.eyeY - cull.focusY);
                appendDistant(_grid, cull, distantFace);
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
        _spriteResidents.clear();
        _nearFaces.clear();
        _spriteCount = 0;
        _spriteTexels = 0;
        _spriteDedicated = 0;
        _loggedUploadFail.clear();
        _batches.clear();
        _batchCount = 0;
        _modelBatches.clear();
        _spriteBatches.clear();
        _distantPages.clear();
        _loggedShader = false;
        _blit.clear();
        _rgba.clear();
        _grid = {};
        _gridReady = false;
        _typeIds.clear();
        _typeIndex.clear();
        _gpu.clear();
        _held.clear();
        _retryAt.clear();
        _backoff.clear();
        _heldCursor = 0;
        _spaceFreed = false;
        _pinned.clear();
        _loadKeys.clear();
        _spriteLru.clear();
        _spriteLruPos.clear();
        _distantPages.clear();
        _vram = LruBudget(kDefaultVramBudgetBytes);
        _frame = 0;
        _stats = {};
        _spriteOverride = nullptr;
        std::lock_guard<std::mutex> guard(_dirtyMutex);
        _dirtyTiles.clear();
        _dirtyAll = true;
    }

    void setScenerySpriteOverride(ScenerySpriteOverride fn)
    {
        _spriteOverride = fn;
    }

    void resetSceneryFrameStats()
    {
        _stats = {};
    }

    SceneryFrameStats sceneryFrameStats()
    {
        return _stats;
    }

    void markAllSceneryDirty()
    {
        std::lock_guard<std::mutex> guard(_dirtyMutex);
        _dirtyAll = true;
        _dirtyTiles.clear();
    }

    bool initSceneryTestDevice(uint16_t width, uint16_t height)
    {
        bgfx::Init device;
        device.type = bgfx::RendererType::Noop;
        device.swapChain.width = width;
        device.swapChain.height = height;
        return bgfx::init(device);
    }

    void sceneryTestFrame()
    {
        bgfx::frame();
    }

    void shutdownSceneryTestDevice()
    {
        bgfx::shutdown();
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
        meshDrawBeginFrame();
        _spriteUploads = 0;
        dropStaleSprites();
        bool dropSprites = false;
        {
            std::lock_guard<std::mutex> guard(_dirtyMutex);
            dropSprites = _dropSprites;
            _dropSprites = false;
        }
        if (dropSprites)
        {
            _spriteResidents.clear();
            _spriteCount = 0;
            _spriteTexels = 0;
            _spriteDedicated = 0;
            _spriteLru.clear();
            _spriteLruPos.clear();
            meshDrawClearSprites();
        }
        FrameCull cull;
        readCamera(cull);
        syncChunks(cull.focusX, cull.focusY);
        capSpriteRadius(cull);
        int32_t placements = 0;
        for (int32_t y = 0; y < _grid.chunksY(); y++)
        {
            for (int32_t x = 0; x < _grid.chunksX(); x++)
            {
                const SceneryChunk* chunk = _grid.at(x, y);
                if (chunk != nullptr && chunk->built)
                {
                    placements += static_cast<int32_t>(chunk->placements.size());
                }
            }
        }
        _stats.placements = placements;
        _pinned.clear();
        _loadKeys.clear();
        composeVisible(cull);
        if (!_loadKeys.empty())
        {
            ensureLoads();
            std::vector<std::string> ids;
            ids.reserve(_loadKeys.size());
            for (uint32_t key : _loadKeys)
            {
                if (key < _typeIds.size())
                {
                    ids.push_back(_typeIds[key]);
                }
            }
            _loads->requestAbsent(ids);
        }
        uploadReadyModels();
        for (size_t i = 0; i < _batchCount; i++)
        {
            const Batch& batch = _batches[i];
            const bool empty = batch.distant ? batch.distantInstances.empty() : batch.instances.empty();
            if (empty || hideBatch(batch))
            {
                continue;
            }
            if (batch.distant)
            {
                meshDrawDistantSprites(
                    batch.page, batch.face, batch.distantInstances.data(),
                    static_cast<uint32_t>(batch.distantInstances.size()));
                _stats.distantSpriteSubmits++;
                _stats.distantSpriteInstances += static_cast<int32_t>(batch.distantInstances.size());
            }
            else if (batch.model)
            {
                meshDrawModel(batch.handle, batch.instances.data(), static_cast<uint32_t>(batch.instances.size()));
                _stats.modelSubmits++;
                _stats.modelInstances += static_cast<int32_t>(batch.instances.size());
            }
            else
            {
                meshDrawSprite(batch.handle, batch.instances.data(), static_cast<uint32_t>(batch.instances.size()));
                _stats.spriteSubmits++;
                _stats.spriteInstances += static_cast<int32_t>(batch.instances.size());
            }
        }
    }
} // namespace OpenRCT2::View3D::Park
