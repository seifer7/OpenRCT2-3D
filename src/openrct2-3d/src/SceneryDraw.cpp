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
#include "MeshDraw.hpp"
#include "ModelCache.hpp"
#include "SpriteQuad.hpp"
#include "ViewerScale.hpp"

#include <cstdio>
#include <cstring>
#include <openrct2/Context.h>
#include <openrct2/GameState.h>
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

        std::unordered_map<std::string, int32_t> _models;
        std::vector<SpriteCache> _sprites;
        std::vector<Batch> _batches;
        uint64_t _signature = 0;
        bool _haveSignature = false;
        bool _loggedShader = false;
        std::vector<Drawing::PaletteIndex> _blit;
        std::vector<uint8_t> _rgba;

        uint64_t mix(uint64_t hash, uint64_t value)
        {
            hash ^= value + 0x9e3779b97f4a7c15uLL + (hash << 6) + (hash >> 2);
            return hash;
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
            }
        };

        const RegisterSceneryModels kRegisterSceneryModels;

        int32_t modelHandle(const std::string& identifier)
        {
            const auto found = _models.find(identifier);
            if (found != _models.end())
            {
                return found->second;
            }
            int32_t handle = -1;
            std::string error;
            const std::optional<CpuMesh> mesh = loadModelForDraw(identifier, modelSourceDirs(), modelCacheDir(), error);
            if (mesh.has_value())
            {
                handle = meshDrawUploadModel(*mesh);
                if (handle < 0)
                {
                    std::fprintf(stderr, "View3D: model %s failed to upload\n", identifier.c_str());
                }
                else
                {
                    std::fprintf(
                        stderr, "View3D: loaded scenery model %s (%zu primitives)\n", identifier.c_str(),
                        mesh->primitives.size());
                }
            }
            else if (!error.empty())
            {
                std::fprintf(stderr, "View3D: model %s failed (%s)\n", identifier.c_str(), error.c_str());
            }
            _models.emplace(identifier, handle);
            return handle;
        }

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
            x = static_cast<float>(tile.x);
            y = static_cast<float>(tile.y);
            // Full-tile objects, including half and three-quarter footprints, sit on
            // the tile centre. That is the GLB origin. Quarter-tile objects sit on
            // the quadrant the 2D painter uses, without the viewport rotation.
            if (entry.flags.has(SmallSceneryFlag::occupiesFullTile))
            {
                x += 0.5f;
                y += 0.5f;
            }
            else
            {
                const uint8_t quadrant = element.getSceneryQuadrant() & 3;
                x += static_cast<float>(SceneryQuadrantOffsets[quadrant].x) / 32.0f;
                y += static_cast<float>(SceneryQuadrantOffsets[quadrant].y) / 32.0f;
            }
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

        uint64_t scenerySignature()
        {
            const TileCoordsXY mapSize = getGameState().mapSize;
            uint64_t hash = mix(0, static_cast<uint32_t>(mapSize.x));
            hash = mix(hash, static_cast<uint32_t>(mapSize.y));
            for (int32_t y = 0; y < mapSize.y; y++)
            {
                for (int32_t x = 0; x < mapSize.x; x++)
                {
                    const TileCoordsXY tile{ x, y };
                    for (const SmallSceneryElement* element : TileElementsView<SmallSceneryElement>(tile))
                    {
                        if (element == nullptr || element->isInvisible())
                        {
                            continue;
                        }
                        const SmallSceneryEntry* entry = element->getEntry();
                        if (entry == nullptr)
                        {
                            continue;
                        }
                        hash = mix(hash, static_cast<uint32_t>(x));
                        hash = mix(hash, static_cast<uint32_t>(y));
                        hash = mix(hash, static_cast<uint32_t>(element->getBaseZ()));
                        hash = mix(hash, element->getEntryIndex());
                        hash = mix(hash, element->getDirection());
                        hash = mix(hash, element->getSceneryQuadrant());
                        hash = mix(hash, witherBand(*entry, element->getAge()));
                        hash = mix(hash, static_cast<uint8_t>(element->getPrimaryColour()));
                        hash = mix(hash, static_cast<uint8_t>(element->getSecondaryColour()));
                        hash = mix(hash, static_cast<uint8_t>(element->getTertiaryColour()));
                        hash = mix(hash, element->isGhost() ? 1u : 0u);
                    }
                }
            }
            return hash;
        }

        void rebuild()
        {
            _batches.clear();
            const TileCoordsXY mapSize = getGameState().mapSize;
            std::unordered_map<std::string, size_t> modelBatches;
            struct SpriteKey
            {
                ImageId image;
                int face = 0;
            };
            std::vector<SpriteKey> spriteKeys;
            std::vector<size_t> spriteBatches;
            int32_t modelObjects = 0;
            int32_t spriteObjects = 0;
            int32_t instances = 0;

            for (int32_t y = 0; y < mapSize.y; y++)
            {
                for (int32_t x = 0; x < mapSize.x; x++)
                {
                    const TileCoordsXY tile{ x, y };
                    for (const SmallSceneryElement* element : TileElementsView<SmallSceneryElement>(tile))
                    {
                        if (element == nullptr || element->isInvisible())
                        {
                            continue;
                        }
                        const SmallSceneryEntry* entry = element->getEntry();
                        if (entry == nullptr)
                        {
                            continue;
                        }
                        float ax;
                        float ay;
                        float az;
                        anchor(tile, *element, *entry, ax, ay, az);
                        const bool vegetation = isVegetation(*entry);

                        std::string identifier;
                        const Object* object = ObjectEntryGetObject(ObjectType::smallScenery, element->getEntryIndex());
                        if (object != nullptr)
                        {
                            identifier = std::string(object->GetIdentifier());
                        }
                        const int32_t model = identifier.empty() ? -1 : modelHandle(identifier);
                        if (model >= 0)
                        {
                            const auto batchIndex = modelBatches.find(identifier);
                            size_t index;
                            if (batchIndex == modelBatches.end())
                            {
                                index = _batches.size();
                                modelBatches.emplace(identifier, index);
                                Batch batch;
                                batch.handle = model;
                                batch.model = true;
                                batch.vegetation = vegetation;
                                _batches.push_back(std::move(batch));
                                modelObjects++;
                            }
                            else
                            {
                                index = batchIndex->second;
                            }
                            _batches[index].instances.push_back(
                                MeshInstance{ ax, ay, az, modelYawRadians(element->getDirection()) });
                            instances++;
                            continue;
                        }

                        spriteObjects++;
                        for (int face = 0; face < 4; face++)
                        {
                            const ImageId image = faceImage(*element, *entry, face);
                            size_t index = spriteKeys.size();
                            bool found = false;
                            for (size_t i = 0; i < spriteKeys.size(); i++)
                            {
                                if (spriteKeys[i].face == face && spriteKeys[i].image == image)
                                {
                                    index = spriteBatches[i];
                                    found = true;
                                    break;
                                }
                            }
                            if (!found)
                            {
                                const int32_t handle = spriteHandleForFace(image, face);
                                if (handle < 0)
                                {
                                    continue;
                                }
                                index = _batches.size();
                                spriteKeys.push_back(SpriteKey{ image, face });
                                spriteBatches.push_back(index);
                                Batch batch;
                                batch.handle = handle;
                                batch.model = false;
                                batch.vegetation = vegetation;
                                _batches.push_back(std::move(batch));
                            }
                            _batches[index].instances.push_back(MeshInstance{ ax, ay, az, 0.0f });
                            instances++;
                        }
                    }
                }
            }
            std::fprintf(
                stderr, "View3D: scenery rebuilt (%d model types, %d sprite objects, %d draws)\n", modelObjects, spriteObjects,
                instances);
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
        meshDrawShutdown();
        _models.clear();
        _sprites.clear();
        _batches.clear();
        _signature = 0;
        _haveSignature = false;
        _loggedShader = false;
        _blit.clear();
        _rgba.clear();
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
        const uint64_t signature = scenerySignature();
        if (!_haveSignature || signature != _signature)
        {
            rebuild();
            _signature = signature;
            _haveSignature = true;
        }
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
