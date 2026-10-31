/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "ParkView.hpp"

#include <algorithm>
#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/SpriteIds.h>
#include <openrct2/config/Config.h>
#include <openrct2/drawing/Drawing.Sprite.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/LightFX.h>
#include <openrct2/drawing/Palette.h>
#include <openrct2/drawing/PaletteType.h>
#include <openrct2/drawing/RenderTarget.h>
#include <openrct2/interface/Viewport.h>
#include <openrct2/interface/Window.h>
#include <openrct2/interface/WindowBase.h>
#include <openrct2/object/ObjectLimits.h>
#include <openrct2/object/TerrainSurfaceObject.h>
#include <openrct2/world/Footpath.h>
#include <openrct2/world/Map.h>
#include <openrct2/world/tile_element/PathElement.h>
#include <openrct2/world/tile_element/Slope.h>
#include <openrct2/world/tile_element/SurfaceElement.h>
#include <vector>

namespace OpenRCT2::View3D::Park
{
    namespace
    {
        constexpr uint32_t kVaryingHash = 0x71A3C0DEu;
        constexpr uint8_t kShaderVersion = 0x0c;
        constexpr int32_t kTextureSize = 32;
        // Flat land sprites are a 64x31 diamond. A 64 square keeps the grid lines and the chequerboard.
        constexpr int32_t kSurfaceSize = 64;
        constexpr uint16_t kSurfaceLayers = 256;
        constexpr uint8_t kKindTop = 0;
        constexpr uint8_t kKindWall = 1;
        constexpr uint8_t kKindWater = 2;
        constexpr uint8_t kKindPath = 3;
        constexpr float kWaterLift = 0.03f;
        // Sits on the land height. A small lift keeps the path off the ground without reading as a step.
        constexpr float kPathLift = 0.03f;
        // Height of one raised land step, in tiles. This is the only height control: land, cliffs, water,
        // paths, and the camera's ground point all come from it. 1.0 is a full tile tall, which read as
        // too steep. 0.5 is half of that.
        constexpr float kLandStepHeight = 0.5f;
        constexpr int32_t kPathCanvasWidth = 64;
        constexpr int32_t kPathCanvasHeight = 32;
        constexpr uint16_t kPathLayers = 256;
        // Same corner rise the 2D painter uses for a sloped path. Even directions run east-west.
        constexpr uint8_t kPathSlopeToLandSlope[4] = {
            kTileSlopeSWSideUp,
            kTileSlopeNWSideUp,
            kTileSlopeNESideUp,
            kTileSlopeSESideUp,
        };
        // Flat path sprite, indexed by edges in the low nibble and corners in the high nibble.
        // Matches the 2D painter. Rotation 0, because the 3D tile is not pre-rotated.
        constexpr uint8_t kPathEdgeOffsets[256] = {
            0, 1, 2, 3, 4, 5, 6,  7,  8, 9,  10, 11, 12, 13, 14, 15, 0, 1, 2, 20, 4, 5, 6, 22, 8, 9, 10, 26, 12, 13, 14, 36,
            0, 1, 2, 3, 4, 5, 21, 23, 8, 9,  10, 11, 12, 13, 33, 37, 0, 1, 2, 3,  4, 5, 6, 24, 8, 9, 10, 11, 12, 13, 14, 38,
            0, 1, 2, 3, 4, 5, 6,  7,  8, 9,  10, 11, 29, 30, 34, 39, 0, 1, 2, 3,  4, 5, 6, 7,  8, 9, 10, 11, 12, 13, 14, 40,
            0, 1, 2, 3, 4, 5, 6,  7,  8, 9,  10, 11, 12, 13, 35, 41, 0, 1, 2, 3,  4, 5, 6, 7,  8, 9, 10, 11, 12, 13, 14, 42,
            0, 1, 2, 3, 4, 5, 6,  7,  8, 25, 10, 27, 12, 31, 14, 43, 0, 1, 2, 3,  4, 5, 6, 7,  8, 9, 10, 28, 12, 13, 14, 44,
            0, 1, 2, 3, 4, 5, 6,  7,  8, 9,  10, 11, 12, 13, 14, 45, 0, 1, 2, 3,  4, 5, 6, 7,  8, 9, 10, 11, 12, 13, 14, 46,
            0, 1, 2, 3, 4, 5, 6,  7,  8, 9,  10, 11, 12, 32, 14, 47, 0, 1, 2, 3,  4, 5, 6, 7,  8, 9, 10, 11, 12, 13, 14, 48,
            0, 1, 2, 3, 4, 5, 6,  7,  8, 9,  10, 11, 12, 13, 14, 49, 0, 1, 2, 3,  4, 5, 6, 7,  8, 9, 10, 11, 12, 13, 14, 50,
        };
        static_assert(sizeof(kPathEdgeOffsets) == 256);
        // 30 degrees down. sin(30) = 1/2, so a flat tile at the centre is twice as wide as it is tall, the 2D
        // isometric diamond. At zoom 0 that diamond is 64 screen pixels wide. The perspective frame shows
        // more of the park than that flat view, so the camera sits a little closer than the geometric match.
        constexpr float kIsoDepression = 0.5235988f;
        constexpr float kTilePixels = 64.0f;
        constexpr float kIsoDistanceScale = 0.8f;
        // Real-time ticks are 25ms. A gap this long means the 3D view was not drawn, so the next frame
        // follows the 2D view again. Shorter hitches while the view stays on do not.
        constexpr uint32_t kSnapGapTicks = 8;
        constexpr float kFovDegrees = 50.0f;
        constexpr float kLookRadiansPerPixel = 0.005f;
        constexpr float kPanTilesPerPixel = 0.06f;
        constexpr float kDollyTilesPerPixel = 0.08f;
        constexpr float kWheelTilesPerNotch = 6.0f;
        constexpr float kMoveTilesPerSecond = 24.0f;
        constexpr float kMinEyeZ = 0.4f;
        constexpr float kMaxPitch = 1.35f;
        constexpr float kQuarterTurn = 1.5707963f;

        struct FlyCamera
        {
            bool live = false;
            float eyeX = 0.0f;
            float eyeY = 0.0f;
            float eyeZ = 8.0f;
            float yaw = -2.3561945f;
            float pitch = -0.7f;
            bool moveForward = false;
            bool moveBack = false;
            bool moveLeft = false;
            bool moveRight = false;
            std::chrono::steady_clock::time_point lastTick{};
            bool haveTick = false;
        };

        FlyCamera _fly;
        uint16_t _frameHeight = 0;
        // Raised when the 3D view is toggled, and until the first frame, so the pose is taken from the 2D view.
        bool _resyncView = true;
        uint32_t _lastLookTick = 0;
        bool _haveLookTick = false;

        void clampFly()
        {
            _fly.pitch = std::clamp(_fly.pitch, -kMaxPitch, kMaxPitch);
            if (_fly.eyeZ < kMinEyeZ)
            {
                _fly.eyeZ = kMinEyeZ;
            }
        }

        void lookVector(float& forwardX, float& forwardY, float& forwardZ)
        {
            const float cosinePitch = std::cos(_fly.pitch);
            forwardX = std::cos(_fly.yaw) * cosinePitch;
            forwardY = std::sin(_fly.yaw) * cosinePitch;
            forwardZ = std::sin(_fly.pitch);
        }

        void moveAlongLook(float distance)
        {
            float forwardX;
            float forwardY;
            float forwardZ;
            lookVector(forwardX, forwardY, forwardZ);
            _fly.eyeX += forwardX * distance;
            _fly.eyeY += forwardY * distance;
            _fly.eyeZ += forwardZ * distance;
            clampFly();
        }

        // Big Z is 16 units per land step. Every mesh height and the camera ground point go through here.
        constexpr float worldZ(int32_t bigZ)
        {
            return static_cast<float>(bigZ) * (kLandStepHeight / 16.0f);
        }
        static_assert(worldZ(16) == kLandStepHeight);

        void seedFlyCamera()
        {
            float targetX = 0.0f;
            float targetY = 0.0f;
            float targetZ = worldZ(32);
            uint8_t rotation = 0;
            float tilePixels = kTilePixels;

            const WindowBase* window = WindowGetMain();
            const Viewport* viewport = window != nullptr ? window->viewport : nullptr;
            if (viewport != nullptr)
            {
                const ScreenCoordsXY centre{ viewport->viewPos.x + viewport->ViewWidth() / 2,
                                             viewport->viewPos.y + viewport->ViewHeight() / 2 };
                int32_t height = 16;
                CoordsXY map = ViewportPosToMapPos(centre, height, viewport->rotation);
                for (int32_t step = 0; step < 4; step++)
                {
                    height = TileElementHeight(map);
                    map = ViewportPosToMapPos(centre, height, viewport->rotation);
                }
                targetX = static_cast<float>(map.x) / 32.0f;
                targetY = static_cast<float>(map.y) / 32.0f;
                targetZ = worldZ(height);
                rotation = viewport->rotation & 3;
                // Zoomed-out levels make one tile cover fewer screen pixels. ViewWidth is the zoomed span.
                const float viewWidth = static_cast<float>(std::max(viewport->ViewWidth(), 1));
                const float screenWidth = static_cast<float>(std::max(viewport->width, 1));
                tilePixels = kTilePixels * screenWidth / viewWidth;
            }
            else
            {
                const TileCoordsXY size = getGameState().mapSize;
                targetX = static_cast<float>(size.x) * 0.5f;
                targetY = static_cast<float>(size.y) * 0.5f;
            }

            const float frameHeight = _frameHeight > 0 ? static_cast<float>(_frameHeight) : 720.0f;
            tilePixels = std::clamp(tilePixels, 4.0f, frameHeight);
            const float cotHalfFov = 1.0f / std::tan(bx::toRad(kFovDegrees * 0.5f));
            // Left-to-right span of one flat tile, in view-plane units, when the eye sits on a tile diagonal.
            constexpr float kDiamondWidth = 1.41421356f;
            float distance = cotHalfFov * kDiamondWidth * frameHeight / (2.0f * tilePixels) * kIsoDistanceScale;
            distance = std::clamp(distance, 1.0f, 4000.0f);

            const float horizontal = distance * std::cos(kIsoDepression);
            const float vertical = distance * std::sin(kIsoDepression);
            // The 2D view rotates the world, then projects it as rotation 0. The eye has to sit on the
            // diagonal that becomes the bottom of that projection, so (1, 1) is rotated back.
            const CoordsXY direction = CoordsXY{ 1, 1 }.rotate(-static_cast<int32_t>(rotation));
            const float scale = std::sqrt(static_cast<float>(direction.x * direction.x + direction.y * direction.y));
            const float dirX = static_cast<float>(direction.x) / scale;
            const float dirY = static_cast<float>(direction.y) / scale;

            _fly.eyeX = targetX + dirX * horizontal;
            _fly.eyeY = targetY + dirY * horizontal;
            _fly.eyeZ = targetZ + vertical;
            _fly.yaw = std::atan2(-dirY, -dirX);
            _fly.pitch = -kIsoDepression;
            _fly.live = true;
            _fly.haveTick = false;
            clampFly();
            std::fprintf(
                stderr, "View3D: camera snapped to the 2D view (rotation %u, %.0f px per tile)\n",
                static_cast<unsigned>(rotation), static_cast<double>(tilePixels));
        }

        void ensureFlyCamera()
        {
            // A toggle is waiting for the next draw, which knows the frame size. Don't take a pose first.
            if (_resyncView || _fly.live)
            {
                return;
            }
            seedFlyCamera();
        }

        void panPixels(float dx, float dy)
        {
            float forwardX;
            float forwardY;
            float forwardZ;
            lookVector(forwardX, forwardY, forwardZ);
            float rightX = forwardY;
            float rightY = -forwardX;
            float rightZ = 0.0f;
            const float rightLength = std::sqrt(rightX * rightX + rightY * rightY);
            if (rightLength > 0.0001f)
            {
                rightX /= rightLength;
                rightY /= rightLength;
            }
            float upX = rightY * forwardZ - rightZ * forwardY;
            float upY = rightZ * forwardX - rightX * forwardZ;
            float upZ = rightX * forwardY - rightY * forwardX;
            const float upLength = std::sqrt(upX * upX + upY * upY + upZ * upZ);
            if (upLength > 0.0001f)
            {
                upX /= upLength;
                upY /= upLength;
                upZ /= upLength;
            }
            // Shift-drag slides the camera with the cursor, on the right and up axes.
            _fly.eyeX += (rightX * dx + upX * dy) * kPanTilesPerPixel;
            _fly.eyeY += (rightY * dx + upY * dy) * kPanTilesPerPixel;
            _fly.eyeZ += (rightZ * dx + upZ * dy) * kPanTilesPerPixel;
            clampFly();
        }

        void applyHeldMovement()
        {
            const auto now = std::chrono::steady_clock::now();
            float dt = 0.0f;
            if (_fly.haveTick)
            {
                dt = std::chrono::duration<float>(now - _fly.lastTick).count();
                dt = std::clamp(dt, 0.0f, 0.1f);
            }
            _fly.lastTick = now;
            _fly.haveTick = true;

            const float along = ((_fly.moveForward ? 1.0f : 0.0f) - (_fly.moveBack ? 1.0f : 0.0f)) * kMoveTilesPerSecond * dt;
            const float side = ((_fly.moveRight ? 1.0f : 0.0f) - (_fly.moveLeft ? 1.0f : 0.0f)) * kMoveTilesPerSecond * dt;
            if (along == 0.0f && side == 0.0f)
            {
                return;
            }
            const float headingX = std::cos(_fly.yaw);
            const float headingY = std::sin(_fly.yaw);
            _fly.eyeX += headingX * along + headingY * side;
            _fly.eyeY += headingY * along - headingX * side;
            clampFly();
        }

        constexpr const char kSurfacesName[] = "s_surfaces";
        constexpr const char kIndexName[] = "s_index";
        constexpr const char kWaterName[] = "s_water";
        constexpr const char kPathsName[] = "s_paths";
        constexpr const char kParamsName[] = "u_params";
        constexpr const char kExtraName[] = "u_extra";

        constexpr const char kVertexShader[] = "uniform mat4 u_modelViewProj;\n"
                                               "in vec3 a_position;\n"
                                               "in vec3 a_normal;\n"
                                               "in vec2 a_texcoord0;\n"
                                               "in vec4 a_color0;\n"
                                               "out vec3 v_world;\n"
                                               "out vec3 v_normal;\n"
                                               "out vec2 v_uv;\n"
                                               "out vec4 v_colour;\n"
                                               "void main()\n"
                                               "{\n"
                                               "    v_world = a_position;\n"
                                               "    v_normal = a_normal;\n"
                                               "    v_uv = a_texcoord0;\n"
                                               "    v_colour = a_color0;\n"
                                               "    gl_Position = u_modelViewProj * vec4(a_position, 1.0);\n"
                                               "}\n";

        constexpr const char kFragmentShader[]
            = "uniform sampler2DArray s_surfaces;\n"
              "uniform sampler2D s_index;\n"
              "uniform sampler2D s_water;\n"
              "uniform sampler2DArray s_paths;\n"
              "uniform vec4 u_params;\n"
              "uniform vec4 u_extra;\n"
              "in vec3 v_world;\n"
              "in vec3 v_normal;\n"
              "in vec2 v_uv;\n"
              "in vec4 v_colour;\n"
              "layout(location = 0) out vec4 bgfx_FragColor;\n"
              "int layerAt(ivec2 tile)\n"
              "{\n"
              "    if (tile.x < 0 || tile.y < 0 || float(tile.x) >= u_params.x || float(tile.y) >= u_params.y)\n"
              "    {\n"
              "        return -1;\n"
              "    }\n"
              "    return int(texelFetch(s_index, tile, 0).r * 255.0 + 0.5);\n"
              "}\n"
              "vec3 sampleSurface(int layer, vec2 uv)\n"
              "{\n"
              "    return texture(s_surfaces, vec3(uv, float(layer))).rgb;\n"
              "}\n"
              "void main()\n"
              "{\n"
              "    float kind = v_colour.g * 255.0;\n"
              "    vec3 colour;\n"
              "    if (kind > 2.5)\n"
              "    {\n"
              "        float layer = float(int(v_colour.r * 255.0 + 0.5));\n"
              "        vec4 pathSample = texture(s_paths, vec3(v_uv, layer));\n"
              "        if (pathSample.a < 0.5)\n"
              "        {\n"
              "            discard;\n"
              "        }\n"
              "        colour = pathSample.rgb;\n"
              "    }\n"
              "    else if (kind > 1.5)\n"
              "    {\n"
              "        colour = texture(s_water, v_uv).rgb;\n"
              "    }\n"
              "    else if (kind > 0.5)\n"
              "    {\n"
              "        colour = sampleSurface(int(v_colour.r * 255.0 + 0.5), v_uv);\n"
              "    }\n"
              "    else\n"
              "    {\n"
              "        vec2 tile = floor(v_world.xy);\n"
              "        vec2 cell = fract(v_world.xy);\n"
              "        int id = layerAt(ivec2(tile));\n"
              "        colour = sampleSurface(max(id, 0), v_uv);\n"
              "        float margin = 0.18;\n"
              "        if (cell.x < margin)\n"
              "        {\n"
              "            int other = layerAt(ivec2(tile) + ivec2(-1, 0));\n"
              "            if (other >= 0)\n"
              "            {\n"
              "                colour = mix(sampleSurface(other, v_uv), colour, smoothstep(0.0, margin, cell.x));\n"
              "            }\n"
              "        }\n"
              "        else if (cell.x > 1.0 - margin)\n"
              "        {\n"
              "            int other = layerAt(ivec2(tile) + ivec2(1, 0));\n"
              "            if (other >= 0)\n"
              "            {\n"
              "                colour = mix(sampleSurface(other, v_uv), colour, smoothstep(0.0, margin, 1.0 - cell.x));\n"
              "            }\n"
              "        }\n"
              "        if (cell.y < margin)\n"
              "        {\n"
              "            int other = layerAt(ivec2(tile) + ivec2(0, -1));\n"
              "            if (other >= 0)\n"
              "            {\n"
              "                colour = mix(sampleSurface(other, v_uv), colour, smoothstep(0.0, margin, cell.y));\n"
              "            }\n"
              "        }\n"
              "        else if (cell.y > 1.0 - margin)\n"
              "        {\n"
              "            int other = layerAt(ivec2(tile) + ivec2(0, 1));\n"
              "            if (other >= 0)\n"
              "            {\n"
              "                colour = mix(sampleSurface(other, v_uv), colour, smoothstep(0.0, margin, 1.0 - cell.y));\n"
              "            }\n"
              "        }\n"
              "    }\n"
              "    if (u_params.z > 0.5 && kind < 0.5)\n"
              "    {\n"
              "        vec2 cell = fract(v_world.xy);\n"
              "        float dx = min(cell.x, 1.0 - cell.x);\n"
              "        float dy = min(cell.y, 1.0 - cell.y);\n"
              "        float px = max(length(vec2(dFdx(v_world.x), dFdy(v_world.x))), 0.0001);\n"
              "        float py = max(length(vec2(dFdx(v_world.y), dFdy(v_world.y))), 0.0001);\n"
              "        float dist = min(dx / px, dy / py);\n"
              "        float line = 1.0 - smoothstep(0.85, 1.7, dist);\n"
              "        float tilePixels = min(1.0 / px, 1.0 / py);\n"
              "        float coverage = mix(0.65, 1.0, smoothstep(2.5, 6.0, tilePixels));\n"
              "        colour = mix(colour, colour * 0.45, line * coverage * 0.42);\n"
              "    }\n"
              "    vec3 normal = normalize(v_normal);\n"
              "    float lit = clamp(dot(normal, normalize(vec3(-0.35, -0.45, 0.82))), 0.0, 1.0);\n"
              "    bool water = kind > 1.5 && kind < 2.5;\n"
              "    float ambient = water ? 0.72 : 0.4;\n"
              "    colour *= (ambient + (1.0 - ambient) * lit) * u_extra.x;\n"
              "    float alpha = water ? u_extra.y : 1.0;\n"
              "    bgfx_FragColor = vec4(colour, alpha);\n"
              "}\n";

        struct Vertex
        {
            float x;
            float y;
            float z;
            float nx;
            float ny;
            float nz;
            float u;
            float v;
            uint8_t surface;
            uint8_t kind;
            uint8_t pad0;
            uint8_t pad1;
        };

        struct ViewOptions
        {
            bool grid = false;
            bool hideBase = false;
            bool hideVertical = false;
            bool underground = false;
            bool transparentWater = false;
        };

        bool _failed = false;
        bool _logged = false;
        bgfx::ProgramHandle _program = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle _surfacesSampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle _indexSampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle _waterSampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle _params = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle _extra = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle _surfaces = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle _index = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle _water = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle _pathSampler = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle _paths = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle _land = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle _lake = BGFX_INVALID_HANDLE;
        bgfx::DynamicVertexBufferHandle _pathMesh = BGFX_INVALID_HANDLE;
        bgfx::VertexLayout _layout;
        uint16_t _indexWidth = 0;
        uint16_t _indexHeight = 0;
        uint16_t _meshWidth = 0;
        uint16_t _meshHeight = 0;
        uint32_t _landCount = 0;
        uint32_t _lakeCount = 0;
        uint32_t _pathCount = 0;
        bool _meshReady = false;
        bool _meshHideBase = false;
        bool _meshHideVertical = false;
        std::vector<uint64_t> _surfaceKeys;
        std::vector<uint64_t> _pathKeys;
        std::vector<Vertex> _landVerts;
        std::vector<Vertex> _lakeVerts;
        std::vector<Vertex> _pathVerts;
        std::vector<uint8_t> _indexBytes;
        std::vector<Drawing::PaletteIndex> _blit;
        std::vector<uint8_t> _rgba;
        // Palette indices of the water overlay. The 2D view animates water by cycling the wave and
        // sparkle entries, so these stay indexed and the texture is recoloured when they change.
        std::vector<Drawing::PaletteIndex> _waterIndices;
        uint8_t _waterWaveRed[10]{};
        uint8_t _waterWaveGreen[10]{};
        uint8_t _waterWaveBlue[10]{};
        bool _waterWaveValid = false;

        void logOnce(const char* message)
        {
            if (_logged)
            {
                return;
            }
            _logged = true;
            std::fprintf(stderr, "View3D: %s\n", message);
        }

        void appendU8(std::vector<uint8_t>& out, uint8_t value)
        {
            out.push_back(value);
        }

        void appendU16(std::vector<uint8_t>& out, uint16_t value)
        {
            out.push_back(static_cast<uint8_t>(value));
            out.push_back(static_cast<uint8_t>(value >> 8));
        }

        void appendU32(std::vector<uint8_t>& out, uint32_t value)
        {
            out.push_back(static_cast<uint8_t>(value));
            out.push_back(static_cast<uint8_t>(value >> 8));
            out.push_back(static_cast<uint8_t>(value >> 16));
            out.push_back(static_cast<uint8_t>(value >> 24));
        }

        void appendName(std::vector<uint8_t>& out, const char* name)
        {
            const auto size = static_cast<uint8_t>(std::strlen(name));
            appendU8(out, size);
            out.insert(out.end(), name, name + size);
        }

        void appendSampler(std::vector<uint8_t>& out, const char* name, uint16_t regIndex, uint8_t dimension)
        {
            appendName(out, name);
            appendU8(out, static_cast<uint8_t>(bgfx::UniformType::Sampler));
            appendU8(out, 1);
            appendU16(out, regIndex);
            appendU16(out, 1);
            appendU8(out, 0);
            appendU8(out, dimension);
            appendU16(out, 0);
        }

        void appendVec4(std::vector<uint8_t>& out, const char* name, uint16_t regIndex)
        {
            appendName(out, name);
            appendU8(out, static_cast<uint8_t>(bgfx::UniformType::Vec4));
            appendU8(out, 1);
            appendU16(out, regIndex);
            appendU16(out, 1);
            appendU16(out, 0);
            appendU16(out, 0);
        }

        bgfx::ShaderHandle createShader(char stage, uint32_t hashIn, uint32_t hashOut, bool fragment, const char* source)
        {
            std::vector<uint8_t> out;
            appendU8(out, static_cast<uint8_t>(stage));
            appendU8(out, static_cast<uint8_t>('S'));
            appendU8(out, static_cast<uint8_t>('H'));
            appendU8(out, kShaderVersion);
            appendU32(out, hashIn);
            appendU32(out, hashOut);
            appendU32(out, 0);
            appendU32(out, 0);
            if (fragment)
            {
                appendU16(out, 6);
                appendSampler(out, kSurfacesName, 0, 3);
                appendSampler(out, kIndexName, 1, 2);
                appendSampler(out, kWaterName, 2, 2);
                appendSampler(out, kPathsName, 3, 3);
                appendVec4(out, kParamsName, 0);
                appendVec4(out, kExtraName, 1);
            }
            else
            {
                appendU16(out, 0);
            }
            const auto sourceSize = static_cast<uint32_t>(std::strlen(source) + 1);
            appendU32(out, sourceSize);
            out.insert(out.end(), source, source + sourceSize);
            return bgfx::createShader(bgfx::copy(out.data(), static_cast<uint32_t>(out.size())));
        }

        ViewOptions readOptions()
        {
            // See-through rides, vehicles, scenery, paths, supports, guests, and staff stay on the viewport
            // flags. Those layers are not drawn yet. Terrain honours the land, cliff, grid, and water flags.
            ViewOptions options;
            const WindowBase* window = WindowGetMain();
            const Viewport* viewport = window != nullptr ? window->viewport : nullptr;
            if (viewport != nullptr)
            {
                options.grid = viewport->flags.has(ViewportFlag::gridlines);
                options.hideBase = viewport->flags.has(ViewportFlag::hideBase);
                options.hideVertical = viewport->flags.has(ViewportFlag::hideVertical);
                options.underground = viewport->flags.has(ViewportFlag::undergroundInside);
            }
            options.transparentWater = Config::Get().general.transparentWater || options.underground;
            return options;
        }

        float cornerZ(const TileCornersZ& corners, int32_t corner)
        {
            switch (corner)
            {
                case 0:
                    return worldZ(corners.south);
                case 1:
                    return worldZ(corners.east);
                case 2:
                    return worldZ(corners.north);
                default:
                    return worldZ(corners.west);
            }
        }

        float neighbourCornerZ(const SurfaceElement* neighbour, int32_t corner)
        {
            if (neighbour == nullptr)
            {
                return worldZ(kMinimumLandZ);
            }
            const TileCornersZ corners = GetSlopeCornerHeights(neighbour->getBaseZ(), neighbour->getSlope());
            return cornerZ(corners, corner);
        }

        // Tracks are not meshed yet. Returning false keeps the cliff whole. When a tunnel is drawn, this
        // is where it omits the height band it passes through. Path tiles sit on top of the land instead.
        bool tunnelBlocksEdge(const TileCoordsXY&, int32_t, float, float)
        {
            return false;
        }

        Vertex makeVertex(float x, float y, float z, float u, float v, uint8_t surface, uint8_t kind)
        {
            Vertex vertex{};
            vertex.x = x;
            vertex.y = y;
            vertex.z = z;
            vertex.u = u;
            vertex.v = v;
            vertex.surface = surface;
            vertex.kind = kind;
            vertex.pad0 = 0;
            vertex.pad1 = 255;
            return vertex;
        }

        void pushTriangle(std::vector<Vertex>& out, Vertex a, Vertex b, Vertex c, float nx, float ny, float nz)
        {
            a.nx = b.nx = c.nx = nx;
            a.ny = b.ny = c.ny = ny;
            a.nz = b.nz = c.nz = nz;
            out.push_back(a);
            out.push_back(b);
            out.push_back(c);
        }

        void pushTop(std::vector<Vertex>& out, Vertex a, Vertex b, Vertex c)
        {
            const float ux = b.x - a.x;
            const float uy = b.y - a.y;
            const float uz = b.z - a.z;
            const float vx = c.x - a.x;
            const float vy = c.y - a.y;
            const float vz = c.z - a.z;
            float nx = uy * vz - uz * vy;
            float ny = uz * vx - ux * vz;
            float nz = ux * vy - uy * vx;
            if (nz < 0.0f)
            {
                nx = -nx;
                ny = -ny;
                nz = -nz;
            }
            const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (length > 0.0001f)
            {
                nx /= length;
                ny /= length;
                nz /= length;
            }
            else
            {
                nx = 0.0f;
                ny = 0.0f;
                nz = 1.0f;
            }
            pushTriangle(out, a, b, c, nx, ny, nz);
        }

        void appendEdge(
            std::vector<Vertex>& out, const TileCoordsXY& tile, const SurfaceElement* neighbour, float x0, float y0, float top0,
            float x1, float y1, float top1, float u0, float u1, int32_t corner0, int32_t corner1, float nx, float ny,
            uint8_t surface)
        {
            float bottom0 = neighbourCornerZ(neighbour, corner0);
            float bottom1 = neighbourCornerZ(neighbour, corner1);
            if (top0 < bottom0)
            {
                bottom0 = top0;
            }
            if (top1 < bottom1)
            {
                bottom1 = top1;
            }
            if (top0 <= bottom0 && top1 <= bottom1)
            {
                return;
            }
            if (tunnelBlocksEdge(tile, corner0, std::min(bottom0, bottom1), std::max(top0, top1)))
            {
                return;
            }

            // One texture repeat per land step, so changing kLandStepHeight does not stretch the cliff.
            const float v0 = top0 / kLandStepHeight;
            const float v1 = top1 / kLandStepHeight;
            const float vb0 = bottom0 / kLandStepHeight;
            const float vb1 = bottom1 / kLandStepHeight;
            const Vertex a = makeVertex(x0, y0, top0, u0, v0, surface, kKindWall);
            const Vertex b = makeVertex(x1, y1, top1, u1, v1, surface, kKindWall);
            const Vertex c = makeVertex(x0, y0, bottom0, u0, vb0, surface, kKindWall);
            const Vertex d = makeVertex(x1, y1, bottom1, u1, vb1, surface, kKindWall);
            pushTriangle(out, a, c, b, nx, ny, 0.0f);
            pushTriangle(out, c, d, b, nx, ny, 0.0f);
        }

        bool diagonalRunsSouthNorth(const CoordsXY& origin, float south, float east, float north, float west)
        {
            const float centre = worldZ(TileElementHeight(origin + CoordsXY{ 16, 16 }));
            const float southNorth = (south + north) * 0.5f;
            const float eastWest = (east + west) * 0.5f;
            return std::abs(centre - southNorth) <= std::abs(centre - eastWest);
        }

        void buildTile(const TileCoordsXY& tile, const SurfaceElement& element, const ViewOptions& options, uint8_t surface)
        {
            const CoordsXY origin = tile.toCoordsXY();
            const TileCornersZ corners = GetSlopeCornerHeights(element.getBaseZ(), element.getSlope());
            const float south = worldZ(corners.south);
            const float east = worldZ(corners.east);
            const float north = worldZ(corners.north);
            const float west = worldZ(corners.west);
            const float x = static_cast<float>(tile.x);
            const float y = static_cast<float>(tile.y);

            if (!options.hideBase)
            {
                const Vertex s = makeVertex(x, y, south, x, y, surface, kKindTop);
                const Vertex e = makeVertex(x + 1.0f, y, east, x + 1.0f, y, surface, kKindTop);
                const Vertex n = makeVertex(x + 1.0f, y + 1.0f, north, x + 1.0f, y + 1.0f, surface, kKindTop);
                const Vertex w = makeVertex(x, y + 1.0f, west, x, y + 1.0f, surface, kKindTop);
                if (diagonalRunsSouthNorth(origin, south, east, north, west))
                {
                    pushTop(_landVerts, s, e, n);
                    pushTop(_landVerts, s, n, w);
                }
                else
                {
                    pushTop(_landVerts, s, e, w);
                    pushTop(_landVerts, e, n, w);
                }
            }

            if (!options.hideVertical)
            {
                const SurfaceElement* southNeighbour = MapGetSurfaceElementAt(TileCoordsXY{ tile.x, tile.y - 1 });
                const SurfaceElement* eastNeighbour = MapGetSurfaceElementAt(TileCoordsXY{ tile.x + 1, tile.y });
                const SurfaceElement* northNeighbour = MapGetSurfaceElementAt(TileCoordsXY{ tile.x, tile.y + 1 });
                const SurfaceElement* westNeighbour = MapGetSurfaceElementAt(TileCoordsXY{ tile.x - 1, tile.y });
                appendEdge(
                    _landVerts, tile, southNeighbour, x, y, south, x + 1.0f, y, east, x, x + 1.0f, 3, 2, 0.0f, -1.0f, surface);
                appendEdge(
                    _landVerts, tile, eastNeighbour, x + 1.0f, y, east, x + 1.0f, y + 1.0f, north, y, y + 1.0f, 0, 3, 1.0f,
                    0.0f, surface);
                appendEdge(
                    _landVerts, tile, northNeighbour, x + 1.0f, y + 1.0f, north, x, y + 1.0f, west, x + 1.0f, x, 1, 0, 0.0f,
                    1.0f, surface);
                appendEdge(
                    _landVerts, tile, westNeighbour, x, y + 1.0f, west, x, y, south, y + 1.0f, y, 2, 1, -1.0f, 0.0f, surface);
            }

            const int32_t waterHeight = element.getWaterHeight();
            if (waterHeight > 0)
            {
                const float water = worldZ(waterHeight) + kWaterLift;
                const Vertex s = makeVertex(x, y, water, x, y, surface, kKindWater);
                const Vertex e = makeVertex(x + 1.0f, y, water, x + 1.0f, y, surface, kKindWater);
                const Vertex n = makeVertex(x + 1.0f, y + 1.0f, water, x + 1.0f, y + 1.0f, surface, kKindWater);
                const Vertex w = makeVertex(x, y + 1.0f, water, x, y + 1.0f, surface, kKindWater);
                pushTriangle(_lakeVerts, s, e, n, 0.0f, 0.0f, 1.0f);
                pushTriangle(_lakeVerts, s, n, w, 0.0f, 0.0f, 1.0f);
            }
        }

        void paintSprite(const ImageId& image, int32_t width, int32_t height)
        {
            _blit.assign(static_cast<size_t>(width) * static_cast<size_t>(height), Drawing::PaletteIndex::transparent);
            Drawing::RenderTarget rt;
            rt.bits = _blit.data();
            rt.width = width;
            rt.height = height;
            rt.pitch = 0;
            rt.zoom_level = ZoomLevel{ 0 };
            const G1Element* sprite = GfxGetG1Element(image);
            ScreenCoordsXY origin{ 0, 0 };
            if (sprite != nullptr)
            {
                origin = { -sprite->xOffset, -sprite->yOffset };
            }
            GfxDrawSpriteSoftware(rt, image, origin);
        }

        const Drawing::GamePalette& getTexturePalette()
        {
            return Drawing::LightFx::IsAvailable() ? Drawing::LightFx::GetPalette() : Drawing::gPalette;
        }

        void writePixel(uint8_t* dst, Drawing::PaletteIndex index, uint8_t fallbackR, uint8_t fallbackG, uint8_t fallbackB)
        {
            if (index == Drawing::PaletteIndex::transparent)
            {
                dst[0] = fallbackR;
                dst[1] = fallbackG;
                dst[2] = fallbackB;
                dst[3] = 255;
                return;
            }
            const Drawing::BGRAColour colour = getTexturePalette()[static_cast<uint8_t>(index)];
            dst[0] = colour.red;
            dst[1] = colour.green;
            dst[2] = colour.blue;
            dst[3] = 255;
        }

        void averageOpaque(const std::vector<Drawing::PaletteIndex>& pixels, uint8_t& red, uint8_t& green, uint8_t& blue)
        {
            uint32_t sumR = 0;
            uint32_t sumG = 0;
            uint32_t sumB = 0;
            uint32_t count = 0;
            for (const Drawing::PaletteIndex index : pixels)
            {
                if (index == Drawing::PaletteIndex::transparent)
                {
                    continue;
                }
                const Drawing::BGRAColour colour = getTexturePalette()[static_cast<uint8_t>(index)];
                sumR += colour.red;
                sumG += colour.green;
                sumB += colour.blue;
                count++;
            }
            if (count == 0)
            {
                red = 90;
                green = 140;
                blue = 70;
                return;
            }
            red = static_cast<uint8_t>(sumR / count);
            green = static_cast<uint8_t>(sumG / count);
            blue = static_cast<uint8_t>(sumB / count);
        }

        void fillSolid(Drawing::PaletteIndex index, int32_t size)
        {
            _rgba.resize(static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
            const bool missing = index == Drawing::PaletteIndex::transparent;
            const Drawing::BGRAColour colour = missing ? Drawing::BGRAColour{ 70, 110, 60, 255 }
                                                       : getTexturePalette()[static_cast<uint8_t>(index)];
            for (size_t i = 0; i < _rgba.size(); i += 4)
            {
                _rgba[i] = colour.red;
                _rgba[i + 1] = colour.green;
                _rgba[i + 2] = colour.blue;
                _rgba[i + 3] = 255;
            }
        }

        // The 2D tile is an isometric diamond. This is the rotation-0 projection that stands that
        // diamond upright on a square texture.
        void flatTileSource(
            int32_t x, int32_t y, int32_t dstSize, int32_t srcWidth, int32_t srcHeight, int32_t& sx, int32_t& sy)
        {
            const float widthScale = static_cast<float>(srcWidth) / 64.0f;
            const float halfWidth = static_cast<float>(srcWidth) * 0.5f;
            const float bottom = static_cast<float>(std::max(srcHeight - 1, 1));
            const float mx = (static_cast<float>(x) + 0.5f) * 32.0f / static_cast<float>(dstSize);
            const float my = (static_cast<float>(y) + 0.5f) * 32.0f / static_cast<float>(dstSize);
            sx = static_cast<int32_t>(std::lround(halfWidth + (my - mx) * widthScale));
            sy = static_cast<int32_t>(std::lround((mx + my) * bottom / 64.0f));
            sx = std::clamp(sx, 0, srcWidth - 1);
            sy = std::clamp(sy, 0, srcHeight - 1);
        }

        void resampleFlatTile(int32_t srcWidth, int32_t srcHeight)
        {
            uint8_t red = 0;
            uint8_t green = 0;
            uint8_t blue = 0;
            averageOpaque(_blit, red, green, blue);
            _rgba.resize(static_cast<size_t>(kSurfaceSize) * static_cast<size_t>(kSurfaceSize) * 4);
            for (int32_t y = 0; y < kSurfaceSize; y++)
            {
                for (int32_t x = 0; x < kSurfaceSize; x++)
                {
                    int32_t sx = 0;
                    int32_t sy = 0;
                    flatTileSource(x, y, kSurfaceSize, srcWidth, srcHeight, sx, sy);
                    const size_t srcOffset = static_cast<size_t>(sy) * static_cast<size_t>(srcWidth) + static_cast<size_t>(sx);
                    const Drawing::PaletteIndex index = _blit[srcOffset];
                    const size_t dstOffset = (static_cast<size_t>(y) * kSurfaceSize + static_cast<size_t>(x)) * 4;
                    writePixel(&_rgba[dstOffset], index, red, green, blue);
                }
            }
        }

        // The flat tile the 2D view draws, including the colour remap. Surfaces with no smoothing
        // pattern used to fall back to a single map colour, which is why the grids and the
        // chequerboard were flat.
        void decodeSurface(const TerrainSurfaceObject& surface)
        {
            const auto selectedColour = surface.getPreviewColour();
            const ImageId image = surface.GetImageId({}, TerrainSurfaceObject::kNoValue, 0, 0, false, false, selectedColour);
            const G1Element* sprite = GfxGetG1Element(image);
            if (sprite == nullptr || sprite->width <= 0 || sprite->height <= 0 || sprite->width > 256 || sprite->height > 256)
            {
                fillSolid(surface.getMapColours(selectedColour)[0], kSurfaceSize);
                return;
            }
            paintSprite(image, sprite->width, sprite->height);
            resampleFlatTile(sprite->width, sprite->height);
        }

        void uploadTexture(bgfx::TextureHandle texture, uint16_t layer, uint16_t size)
        {
            const bgfx::Memory* memory = bgfx::copy(_rgba.data(), static_cast<uint32_t>(_rgba.size()));
            bgfx::updateTexture2D(texture, layer, 0, 0, 0, size, size, memory);
        }

        // The overlay sprite is the same isometric diamond as a land tile. Stretching that rectangle
        // onto the square leaves the sprite's empty corners in the texture, and those corners meet as
        // the grey diamonds. Sample it through the land projection instead, and keep the palette index
        // so the wave colours can still cycle.
        void storeWaterIndices(int32_t srcWidth, int32_t srcHeight)
        {
            _waterIndices.assign(
                static_cast<size_t>(kTextureSize) * static_cast<size_t>(kTextureSize), Drawing::PaletteIndex::transparent);
            for (int32_t y = 0; y < kTextureSize; y++)
            {
                for (int32_t x = 0; x < kTextureSize; x++)
                {
                    int32_t sx = 0;
                    int32_t sy = 0;
                    flatTileSource(x, y, kTextureSize, srcWidth, srcHeight, sx, sy);
                    const size_t srcOffset = static_cast<size_t>(sy) * static_cast<size_t>(srcWidth) + static_cast<size_t>(sx);
                    const size_t dstIndex = static_cast<size_t>(y) * kTextureSize + static_cast<size_t>(x);
                    _waterIndices[dstIndex] = _blit[srcOffset];
                }
            }
        }

        // Expands the stored water indices through the live palette. Upload only when the ten wave and
        // sparkle colours differ from the last upload, which is also how the cycle stops while paused.
        void refreshWaterTexture()
        {
            if (_waterIndices.size() != static_cast<size_t>(kTextureSize) * static_cast<size_t>(kTextureSize)
                || !bgfx::isValid(_water))
            {
                return;
            }

            constexpr int32_t kAnimatedColours = 10;
            const uint8_t base = static_cast<uint8_t>(Drawing::PaletteIndex::waterWaves0);
            uint8_t red[kAnimatedColours];
            uint8_t green[kAnimatedColours];
            uint8_t blue[kAnimatedColours];
            bool changed = !_waterWaveValid;
            for (int32_t i = 0; i < kAnimatedColours; i++)
            {
                const Drawing::BGRAColour colour = getTexturePalette()[static_cast<uint8_t>(base + i)];
                red[i] = colour.red;
                green[i] = colour.green;
                blue[i] = colour.blue;
                if (!changed
                    && (colour.red != _waterWaveRed[i] || colour.green != _waterWaveGreen[i]
                        || colour.blue != _waterWaveBlue[i]))
                {
                    changed = true;
                }
            }
            if (!changed)
            {
                return;
            }

            std::copy(red, red + kAnimatedColours, _waterWaveRed);
            std::copy(green, green + kAnimatedColours, _waterWaveGreen);
            std::copy(blue, blue + kAnimatedColours, _waterWaveBlue);
            _waterWaveValid = true;

            uint8_t fillR = 0;
            uint8_t fillG = 0;
            uint8_t fillB = 0;
            averageOpaque(_waterIndices, fillR, fillG, fillB);
            _rgba.resize(static_cast<size_t>(kTextureSize) * static_cast<size_t>(kTextureSize) * 4);
            for (int32_t y = 0; y < kTextureSize; y++)
            {
                for (int32_t x = 0; x < kTextureSize; x++)
                {
                    const size_t dstIndex = static_cast<size_t>(y) * kTextureSize + static_cast<size_t>(x);
                    const Drawing::PaletteIndex index = _waterIndices[dstIndex];
                    const size_t dstOffset = dstIndex * 4;
                    writePixel(&_rgba[dstOffset], index, fillR, fillG, fillB);
                }
            }
            uploadTexture(_water, 0, kTextureSize);
        }

        void decodeWater()
        {
            const ImageId image(SPR_G2_OPAQUE_WATER_OVERLAY);
            const G1Element* sprite = GfxGetG1Element(image);
            _waterIndices.clear();
            _waterWaveValid = false;
            if (sprite == nullptr || sprite->width <= 0 || sprite->height <= 0)
            {
                fillSolid(Drawing::PaletteIndex::transparent, kTextureSize);
                return;
            }
            const int32_t width = std::min<int32_t>(sprite->width, 256);
            const int32_t height = std::min<int32_t>(sprite->height, 256);
            paintSprite(image, width, height);
            storeWaterIndices(width, height);
            refreshWaterTexture();
        }

        // Copies every loaded surface pattern, and the water overlay, into the GPU cache. Later frames sample
        // that cache. Toggling the 3D view off drops it, so the next start reads the graphics pack again.
        void cacheGraphicsPack()
        {
            for (uint16_t surface = 0; surface < kMaxTerrainSurfaceObjects; surface++)
            {
                const TerrainSurfaceObject* object = TerrainSurfaceObject::GetById(surface);
                if (object == nullptr)
                {
                    fillSolid(Drawing::PaletteIndex::transparent, kSurfaceSize);
                }
                else
                {
                    decodeSurface(*object);
                }
                uploadTexture(_surfaces, surface, kSurfaceSize);
            }
            fillSolid(Drawing::PaletteIndex::transparent, kSurfaceSize);
            uploadTexture(_surfaces, kMaxTerrainSurfaceObjects, kSurfaceSize);
            decodeWater();
            uploadTexture(_water, 0, kTextureSize);
            std::fprintf(stderr, "View3D: cached terrain and water images from the loaded graphics\n");
        }

        bool ensureResources()
        {
            if (_failed)
            {
                return false;
            }
            if (bgfx::isValid(_program))
            {
                return true;
            }

            _surfacesSampler = bgfx::createUniform(kSurfacesName, bgfx::UniformType::Sampler);
            _indexSampler = bgfx::createUniform(kIndexName, bgfx::UniformType::Sampler);
            _waterSampler = bgfx::createUniform(kWaterName, bgfx::UniformType::Sampler);
            _pathSampler = bgfx::createUniform(kPathsName, bgfx::UniformType::Sampler);
            _params = bgfx::createUniform(kParamsName, bgfx::UniformType::Vec4);
            _extra = bgfx::createUniform(kExtraName, bgfx::UniformType::Vec4);
            const bgfx::ShaderHandle vertex = createShader('V', 0, kVaryingHash, false, kVertexShader);
            const bgfx::ShaderHandle fragment = createShader('F', kVaryingHash, 0, true, kFragmentShader);
            _program = bgfx::createProgram(vertex, fragment, true);
            if (!bgfx::isValid(_program))
            {
                logOnce("terrain shader failed");
                _failed = true;
                return false;
            }

            _layout.begin(bgfx::RendererType::OpenGL)
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
                .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
                .end();
            static_assert(sizeof(Vertex) == 36);
            if (_layout.getStride() != sizeof(Vertex))
            {
                logOnce("terrain vertex layout does not match");
                _failed = true;
                return false;
            }

            const uint64_t tileFlags = BGFX_SAMPLER_POINT;
            _surfaces = bgfx::createTexture2D(
                kSurfaceSize, kSurfaceSize, false, kSurfaceLayers, bgfx::TextureFormat::RGBA8, tileFlags);
            _water = bgfx::createTexture2D(kTextureSize, kTextureSize, false, 1, bgfx::TextureFormat::RGBA8, tileFlags);
            const uint64_t pathFlags = BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
            _paths = bgfx::createTexture2D(
                kSurfaceSize, kSurfaceSize, false, kPathLayers, bgfx::TextureFormat::RGBA8, pathFlags);
            _land = bgfx::createDynamicVertexBuffer(1, _layout, BGFX_BUFFER_ALLOW_RESIZE);
            _lake = bgfx::createDynamicVertexBuffer(1, _layout, BGFX_BUFFER_ALLOW_RESIZE);
            _pathMesh = bgfx::createDynamicVertexBuffer(1, _layout, BGFX_BUFFER_ALLOW_RESIZE);
            if (!bgfx::isValid(_surfaces) || !bgfx::isValid(_water) || !bgfx::isValid(_paths) || !bgfx::isValid(_land)
                || !bgfx::isValid(_lake) || !bgfx::isValid(_pathMesh))
            {
                logOnce("terrain resources failed");
                _failed = true;
                return false;
            }
            cacheGraphicsPack();
            return true;
        }

        void ensureIndexTexture(uint16_t width, uint16_t height)
        {
            if (bgfx::isValid(_index) && (_indexWidth != width || _indexHeight != height))
            {
                bgfx::destroy(_index);
                _index = BGFX_INVALID_HANDLE;
            }
            if (bgfx::isValid(_index))
            {
                return;
            }
            _index = bgfx::createTexture2D(
                width, height, false, 1, bgfx::TextureFormat::R8, BGFX_SAMPLER_POINT | BGFX_SAMPLER_UVW_CLAMP);
            _indexWidth = width;
            _indexHeight = height;
        }

        uint64_t pathKeyAt(const TileCoordsXY& tile);

        // Height, slope, water, and surface type. Bit 0 marks a tile that has a surface. Grass length is
        // left out, so it can grow without rebuilding the mesh.
        uint64_t surfaceKey(const SurfaceElement* element)
        {
            if (element == nullptr)
            {
                return 0;
            }
            return 1uLL | (static_cast<uint64_t>(element->baseHeight) << 8) | (static_cast<uint64_t>(element->getSlope()) << 16)
                | (static_cast<uint64_t>(static_cast<uint32_t>(element->getWaterHeight())) << 24)
                | (static_cast<uint64_t>(element->getSurfaceObjectIndex()) << 40);
        }

        bool terrainCacheMatches(const TileCoordsXY& mapSize, uint16_t width, uint16_t height, const ViewOptions& options)
        {
            if (!_meshReady || _meshWidth != width || _meshHeight != height || _meshHideBase != options.hideBase
                || _meshHideVertical != options.hideVertical)
            {
                return false;
            }
            const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
            if (_surfaceKeys.size() != count || _pathKeys.size() != count)
            {
                return false;
            }
            for (int32_t y = 0; y < mapSize.y; y++)
            {
                for (int32_t x = 0; x < mapSize.x; x++)
                {
                    const SurfaceElement* element = MapGetSurfaceElementAt(TileCoordsXY{ x, y });
                    const size_t index = static_cast<size_t>(y) * width + static_cast<size_t>(x);
                    if (surfaceKey(element) != _surfaceKeys[index] || pathKeyAt({ x, y }) != _pathKeys[index])
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        void uploadMesh(bgfx::DynamicVertexBufferHandle buffer, const std::vector<Vertex>& vertices)
        {
            if (vertices.empty() || !bgfx::isValid(buffer))
            {
                return;
            }
            const bgfx::Memory* memory = bgfx::copy(vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(Vertex)));
            bgfx::update(buffer, 0, memory);
        }

        // The 2D sprite is drawn from the top corner of the diamond. One pixel of dilation closes the
        // gap where nearest-neighbour sampling misses that edge, so neighbouring tiles still meet.
        void dilatePathCanvas()
        {
            const std::vector<Drawing::PaletteIndex> src = _blit;
            constexpr int32_t kWidth = kPathCanvasWidth;
            constexpr int32_t kHeight = kPathCanvasHeight;
            for (int32_t y = 0; y < kHeight; y++)
            {
                for (int32_t x = 0; x < kWidth; x++)
                {
                    const size_t index = static_cast<size_t>(y) * kWidth + static_cast<size_t>(x);
                    if (src[index] != Drawing::PaletteIndex::transparent)
                    {
                        continue;
                    }
                    Drawing::PaletteIndex found = Drawing::PaletteIndex::transparent;
                    for (int32_t dy = -1; dy <= 1 && found == Drawing::PaletteIndex::transparent; dy++)
                    {
                        for (int32_t dx = -1; dx <= 1; dx++)
                        {
                            const int32_t yy = y + dy;
                            const int32_t xx = x + dx;
                            if (yy < 0 || xx < 0 || yy >= kHeight || xx >= kWidth)
                            {
                                continue;
                            }
                            const size_t neighbourIndex = static_cast<size_t>(yy) * kWidth + static_cast<size_t>(xx);
                            const Drawing::PaletteIndex neighbour = src[neighbourIndex];
                            if (neighbour != Drawing::PaletteIndex::transparent)
                            {
                                found = neighbour;
                                break;
                            }
                        }
                    }
                    _blit[index] = found;
                }
            }
        }

        void resamplePathCanvas()
        {
            _rgba.assign(static_cast<size_t>(kSurfaceSize) * static_cast<size_t>(kSurfaceSize) * 4, 0);
            const float bottom = static_cast<float>(std::max(kPathCanvasHeight - 1, 1));
            const float halfWidth = static_cast<float>(kPathCanvasWidth) * 0.5f;
            const float widthScale = static_cast<float>(kPathCanvasWidth) / 64.0f;
            for (int32_t y = 0; y < kSurfaceSize; y++)
            {
                for (int32_t x = 0; x < kSurfaceSize; x++)
                {
                    const float mx = (static_cast<float>(x) + 0.5f) * 32.0f / static_cast<float>(kSurfaceSize);
                    const float my = (static_cast<float>(y) + 0.5f) * 32.0f / static_cast<float>(kSurfaceSize);
                    int32_t sx = static_cast<int32_t>(std::lround(halfWidth + (my - mx) * widthScale));
                    int32_t sy = static_cast<int32_t>(std::lround((mx + my) * bottom / 64.0f));
                    sx = std::clamp(sx, 0, kPathCanvasWidth - 1);
                    sy = std::clamp(sy, 0, kPathCanvasHeight - 1);
                    const size_t srcOffset = static_cast<size_t>(sy) * kPathCanvasWidth + static_cast<size_t>(sx);
                    const Drawing::PaletteIndex index = _blit[srcOffset];
                    const size_t dstOffset = (static_cast<size_t>(y) * kSurfaceSize + static_cast<size_t>(x)) * 4;
                    if (index == Drawing::PaletteIndex::transparent)
                    {
                        continue;
                    }
                    const Drawing::BGRAColour colour = getTexturePalette()[static_cast<uint8_t>(index)];
                    _rgba[dstOffset] = colour.red;
                    _rgba[dstOffset + 1] = colour.green;
                    _rgba[dstOffset + 2] = colour.blue;
                    _rgba[dstOffset + 3] = 255;
                }
            }
        }

        bool paintPathCanvas(uint32_t image)
        {
            const G1Element* sprite = GfxGetG1Element(ImageId(image));
            _blit.assign(static_cast<size_t>(kPathCanvasWidth) * kPathCanvasHeight, Drawing::PaletteIndex::transparent);
            if (sprite == nullptr || sprite->width <= 0 || sprite->height <= 0)
            {
                return false;
            }
            Drawing::RenderTarget rt;
            rt.bits = _blit.data();
            rt.width = kPathCanvasWidth;
            rt.height = kPathCanvasHeight;
            rt.pitch = 0;
            rt.zoom_level = ZoomLevel{ 0 };
            // GfxDrawSpriteSoftware adds the sprite offset itself. The origin is the top corner of the
            // rotation-0 diamond, so the connection sits on the same part of the tile as in the 2D view.
            const ScreenCoordsXY origin{ 32, 0 };
            GfxDrawSpriteSoftware(rt, ImageId(image), origin);
            return true;
        }

        void decodePathImage(uint32_t image)
        {
            if (!paintPathCanvas(image))
            {
                _rgba.assign(static_cast<size_t>(kSurfaceSize) * static_cast<size_t>(kSurfaceSize) * 4, 0);
                return;
            }
            dilatePathCanvas();
            resamplePathCanvas();
        }

        // Queue paths ignore corner fillers. A slope uses the straight sprite for that axis and the
        // mesh carries the rise, so the ramp joins the flat pieces on either end.
        bool pathSurfaceImage(const PathElement& path, uint32_t& image)
        {
            const PathSurfaceDescriptor* descriptor = path.getSurfaceDescriptor();
            if (descriptor == nullptr)
            {
                return false;
            }
            uint8_t edges = path.getEdges() & 0x0F;
            uint8_t corners = path.getCorners() & 0x0F;
            if (path.isSloped())
            {
                edges = (path.getSlopeDirection() & 1) == 0 ? 0x05 : 0x0A;
                corners = 0;
            }
            else if (path.isQueue())
            {
                corners = 0;
            }
            const uint8_t mask = static_cast<uint8_t>(edges | (corners << 4));
            image = descriptor->image + kPathEdgeOffsets[mask];
            return true;
        }

        uint64_t pathKeyAt(const TileCoordsXY& tile)
        {
            uint64_t hash = 14695981039346656037uLL;
            const TileElement* element = MapGetFirstElementAt(tile);
            if (element == nullptr)
            {
                return hash;
            }
            do
            {
                if (element->getType() != TileElementType::path || element->isGhost() || element->isInvisible())
                {
                    continue;
                }
                const PathElement* path = element->asPath();
                uint32_t image = 0;
                pathSurfaceImage(*path, image);
                hash ^= static_cast<uint64_t>(path->getBaseZ());
                hash *= 1099511628211uLL;
                hash ^= path->getEdgesAndCorners();
                hash *= 1099511628211uLL;
                hash ^= path->isSloped() ? (0x100uLL | path->getSlopeDirection()) : 0uLL;
                hash *= 1099511628211uLL;
                hash ^= image;
                hash *= 1099511628211uLL;
            } while (!(element++)->isLastForTile());
            return hash;
        }

        void appendPathTile(const TileCoordsXY& tile, const PathElement& path, uint8_t layer)
        {
            uint8_t slope = 0;
            if (path.isSloped())
            {
                slope = kPathSlopeToLandSlope[path.getSlopeDirection() & 3];
            }
            const TileCornersZ corners = GetSlopeCornerHeights(path.getBaseZ(), slope);
            const float x = static_cast<float>(tile.x);
            const float y = static_cast<float>(tile.y);
            const float south = worldZ(corners.south) + kPathLift;
            const float east = worldZ(corners.east) + kPathLift;
            const float north = worldZ(corners.north) + kPathLift;
            const float west = worldZ(corners.west) + kPathLift;
            const Vertex s = makeVertex(x, y, south, 0.0f, 0.0f, layer, kKindPath);
            const Vertex e = makeVertex(x + 1.0f, y, east, 1.0f, 0.0f, layer, kKindPath);
            const Vertex n = makeVertex(x + 1.0f, y + 1.0f, north, 1.0f, 1.0f, layer, kKindPath);
            const Vertex w = makeVertex(x, y + 1.0f, west, 0.0f, 1.0f, layer, kKindPath);
            pushTop(_pathVerts, s, e, n);
            pushTop(_pathVerts, s, n, w);
        }

        void rebuildPaths(const TileCoordsXY& mapSize)
        {
            _pathVerts.clear();
            if (!bgfx::isValid(_paths))
            {
                return;
            }
            std::vector<uint32_t> images;
            bool cacheFull = false;
            for (int32_t y = 0; y < mapSize.y; y++)
            {
                for (int32_t x = 0; x < mapSize.x; x++)
                {
                    const TileCoordsXY tile{ x, y };
                    const TileElement* element = MapGetFirstElementAt(tile);
                    if (element == nullptr)
                    {
                        continue;
                    }
                    do
                    {
                        if (element->getType() != TileElementType::path || element->isGhost() || element->isInvisible())
                        {
                            continue;
                        }
                        const PathElement* path = element->asPath();
                        uint32_t image = 0;
                        if (!pathSurfaceImage(*path, image))
                        {
                            continue;
                        }
                        int32_t layer = -1;
                        for (size_t i = 0; i < images.size(); i++)
                        {
                            if (images[i] == image)
                            {
                                layer = static_cast<int32_t>(i);
                                break;
                            }
                        }
                        if (layer < 0)
                        {
                            if (images.size() >= kPathLayers)
                            {
                                cacheFull = true;
                                continue;
                            }
                            layer = static_cast<int32_t>(images.size());
                            images.push_back(image);
                            decodePathImage(image);
                            uploadTexture(_paths, static_cast<uint16_t>(layer), kSurfaceSize);
                        }
                        appendPathTile(tile, *path, static_cast<uint8_t>(layer));
                    } while (!(element++)->isLastForTile());
                }
            }
            if (cacheFull)
            {
                logOnce("path image cache is full");
            }
            uploadMesh(_pathMesh, _pathVerts);
            _pathCount = static_cast<uint32_t>(_pathVerts.size());
            _pathVerts.clear();
            std::fprintf(stderr, "View3D: rebuilt path mesh (%u vertices, %zu images)\n", _pathCount, images.size());
        }

        void rebuildTerrain(const TileCoordsXY& mapSize, uint16_t width, uint16_t height, const ViewOptions& options)
        {
            _landVerts.clear();
            _lakeVerts.clear();
            _indexBytes.assign(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
            _surfaceKeys.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
            _pathKeys.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
            for (int32_t y = 0; y < mapSize.y; y++)
            {
                for (int32_t x = 0; x < mapSize.x; x++)
                {
                    const TileCoordsXY tile{ x, y };
                    const size_t index = static_cast<size_t>(y) * width + static_cast<size_t>(x);
                    const SurfaceElement* element = MapGetSurfaceElementAt(tile);
                    _surfaceKeys[index] = surfaceKey(element);
                    _pathKeys[index] = pathKeyAt(tile);
                    if (element == nullptr)
                    {
                        continue;
                    }
                    const uint8_t surface = static_cast<uint8_t>(element->getSurfaceObjectIndex());
                    _indexBytes[index] = surface;
                    buildTile(tile, *element, options, surface);
                }
            }

            const bgfx::Memory* indices = bgfx::copy(_indexBytes.data(), static_cast<uint32_t>(_indexBytes.size()));
            bgfx::updateTexture2D(_index, 0, 0, 0, 0, width, height, indices);
            uploadMesh(_land, _landVerts);
            uploadMesh(_lake, _lakeVerts);
            rebuildPaths(mapSize);
            _landCount = static_cast<uint32_t>(_landVerts.size());
            _lakeCount = static_cast<uint32_t>(_lakeVerts.size());
            _landVerts.clear();
            _lakeVerts.clear();
            _meshWidth = width;
            _meshHeight = height;
            _meshHideBase = options.hideBase;
            _meshHideVertical = options.hideVertical;
            _meshReady = true;
            std::fprintf(stderr, "View3D: rebuilt terrain mesh (%u x %u)\n", width, height);
        }

        void submitMesh(
            bgfx::DynamicVertexBufferHandle buffer, uint32_t count, uint32_t depth, uint64_t state, const float* params,
            const float* extra)
        {
            if (count == 0 || !bgfx::isValid(buffer))
            {
                return;
            }
            bgfx::setState(state);
            bgfx::setTexture(0, _surfacesSampler, _surfaces);
            bgfx::setTexture(1, _indexSampler, _index);
            bgfx::setTexture(2, _waterSampler, _water);
            bgfx::setTexture(3, _pathSampler, _paths);
            bgfx::setUniform(_params, params);
            bgfx::setUniform(_extra, extra);
            bgfx::setVertexBuffer(0, buffer, 0, count);
            bgfx::submit(0, _program, depth);
        }

        void lookAtMainView(uint16_t backWidth, uint16_t backHeight)
        {
            _frameHeight = backHeight;
            const uint32_t tick = gCurrentRealTimeTicks;
            // Drawing stops while the 2D view is up. The next 3D frame follows that view, even when the
            // toggle itself did not get to raise the flag.
            const bool drawingResumed = _haveLookTick && tick - _lastLookTick > kSnapGapTicks;
            _lastLookTick = tick;
            _haveLookTick = true;
            if (!_fly.live || _resyncView || drawingResumed)
            {
                seedFlyCamera();
                _resyncView = false;
            }
            applyHeldMovement();

            float forwardX;
            float forwardY;
            float forwardZ;
            lookVector(forwardX, forwardY, forwardZ);

            float view[16];
            float projection[16];
            bx::mtxLookAt(
                view, bx::Vec3(_fly.eyeX, _fly.eyeY, _fly.eyeZ),
                bx::Vec3(_fly.eyeX + forwardX, _fly.eyeY + forwardY, _fly.eyeZ + forwardZ), bx::Vec3(0.0f, 0.0f, 1.0f));
            // The left-handed look puts world +X on the right. The 2D view, from the same corner,
            // puts world +X on the left. Negating camera X matches that view. Depth is the Z row.
            view[0] = -view[0];
            view[4] = -view[4];
            view[8] = -view[8];
            view[12] = -view[12];
            const float aspect = backHeight > 0 ? static_cast<float>(backWidth) / static_cast<float>(backHeight) : 1.0f;
            // bx::mtxProj converts this angle with toRad, so the value is degrees. Passing radians
            // collapses the field of view to under one degree and fills the window with one tile.
            bx::mtxProj(projection, kFovDegrees, aspect, 0.2f, 8000.0f, bgfx::getCaps()->homogeneousDepth);
            bgfx::setViewTransform(0, view, projection);
        }
    } // namespace

    void cameraDrag(int32_t dx, int32_t dy, bool shift, bool ctrl)
    {
        ensureFlyCamera();
        if (shift)
        {
            panPixels(static_cast<float>(dx), static_cast<float>(dy));
            return;
        }
        _fly.yaw -= static_cast<float>(dx) * kLookRadiansPerPixel;
        if (ctrl)
        {
            moveAlongLook(-static_cast<float>(dy) * kDollyTilesPerPixel);
            return;
        }
        _fly.pitch -= static_cast<float>(dy) * kLookRadiansPerPixel;
        clampFly();
    }

    void cameraScroll(int32_t wheelSteps)
    {
        ensureFlyCamera();
        moveAlongLook(static_cast<float>(-wheelSteps) * kWheelTilesPerNotch);
    }

    void cameraTurnQuarter(int32_t direction)
    {
        ensureFlyCamera();
        _fly.yaw -= static_cast<float>(direction) * kQuarterTurn;
    }

    void cameraSetMovement(bool forward, bool back, bool left, bool right)
    {
        _fly.moveForward = forward;
        _fly.moveBack = back;
        _fly.moveLeft = left;
        _fly.moveRight = right;
    }

    void matchMainView()
    {
        _resyncView = true;
        _fly.live = false;
        _fly.haveTick = false;
    }

    void release()
    {
        _fly = {};
        _resyncView = true;
        _failed = false;
        _logged = false;
        auto destroyTexture = [](bgfx::TextureHandle& handle) {
            if (bgfx::isValid(handle))
            {
                bgfx::destroy(handle);
            }
            handle = BGFX_INVALID_HANDLE;
        };
        auto destroyUniform = [](bgfx::UniformHandle& handle) {
            if (bgfx::isValid(handle))
            {
                bgfx::destroy(handle);
            }
            handle = BGFX_INVALID_HANDLE;
        };
        auto destroyBuffer = [](bgfx::DynamicVertexBufferHandle& handle) {
            if (bgfx::isValid(handle))
            {
                bgfx::destroy(handle);
            }
            handle = BGFX_INVALID_HANDLE;
        };
        destroyTexture(_surfaces);
        destroyTexture(_index);
        destroyTexture(_water);
        destroyTexture(_paths);
        destroyBuffer(_land);
        destroyBuffer(_lake);
        destroyBuffer(_pathMesh);
        if (bgfx::isValid(_program))
        {
            bgfx::destroy(_program);
        }
        _program = BGFX_INVALID_HANDLE;
        destroyUniform(_surfacesSampler);
        destroyUniform(_indexSampler);
        destroyUniform(_waterSampler);
        destroyUniform(_pathSampler);
        destroyUniform(_params);
        destroyUniform(_extra);
        _indexWidth = 0;
        _indexHeight = 0;
        _meshWidth = 0;
        _meshHeight = 0;
        _landCount = 0;
        _lakeCount = 0;
        _pathCount = 0;
        _meshReady = false;
        _meshHideBase = false;
        _meshHideVertical = false;
        _surfaceKeys.clear();
        _pathKeys.clear();
        _landVerts.clear();
        _lakeVerts.clear();
        _pathVerts.clear();
        _waterIndices.clear();
        _waterWaveValid = false;
    }

    bool draw(uint16_t backWidth, uint16_t backHeight)
    {
        if (!ensureResources())
        {
            return false;
        }

        const ViewOptions options = readOptions();
        const TileCoordsXY mapSize = getGameState().mapSize;
        if (mapSize.x <= 0 || mapSize.y <= 0 || mapSize.x > 4096 || mapSize.y > 4096)
        {
            return false;
        }
        const auto width = static_cast<uint16_t>(mapSize.x);
        const auto height = static_cast<uint16_t>(mapSize.y);
        ensureIndexTexture(width, height);
        if (!bgfx::isValid(_index))
        {
            return false;
        }

        // The mesh stays on the GPU until a surface tile changes. Grid, underground view, and water
        // transparency are uniforms, so those do not rebuild it.
        if (!terrainCacheMatches(mapSize, width, height, options))
        {
            rebuildTerrain(mapSize, width, height, options);
        }

        refreshWaterTexture();
        lookAtMainView(backWidth, backHeight);
        bgfx::setViewMode(0, bgfx::ViewMode::Sequential);

        const float darken = options.underground ? 0.62f : 1.0f;
        const float waterAlpha = options.transparentWater ? 0.45f : 0.9f;
        const float extra[4] = { darken, waterAlpha, 0.0f, 0.0f };
        const float params[4] = { static_cast<float>(width), static_cast<float>(height), options.grid ? 1.0f : 0.0f, 0.0f };

        const uint64_t opaque = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
        const uint64_t blended = opaque | BGFX_STATE_BLEND_ALPHA;
        // Paths go on before the water. A path under the surface then shows through it, and a path
        // at the same height covers it. Discarded path texels leave the land depth alone.
        submitMesh(_land, _landCount, 0, opaque, params, extra);
        submitMesh(_pathMesh, _pathCount, 1, opaque, params, extra);
        submitMesh(_lake, _lakeCount, 2, blended, params, extra);
        return true;
    }
} // namespace OpenRCT2::View3D::Park
