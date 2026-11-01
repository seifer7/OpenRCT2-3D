/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <cmath>
#include <cstdint>

namespace OpenRCT2::View3D
{
    // One land step (16 sim Z) in viewer units. This is the only vertical scale.
    // Terrain, water, paths, the camera ground point, scenery placement, sprite
    // fallbacks, and GLB height all follow it. 1.0 is a full tile tall and read as
    // too steep beside the 2D view. 0.5 is half of that, so viewer Z is sim Z / 32,
    // the same as X and Y. See docs/agents/scenery-3d-milestone.md.
    constexpr float kLandStepHeight = 0.5f;

    // Sim Z to viewer Z. Equals kLandStepHeight / 16.
    constexpr float kViewerZPerSimUnit = kLandStepHeight / 16.0f;

    constexpr float viewerZ(int32_t simZ)
    {
        return static_cast<float>(simZ) * kViewerZPerSimUnit;
    }

    // GLB scenery is authored at sim/32 on every axis (1 Blender unit = 1 tile, and
    // one land step = 0.5). Multiplying authored Z by this keeps that height lined
    // up with the terrain if kLandStepHeight changes. It is 1 at 0.5. X and Y stay
    // 1 Blender unit per tile.
    constexpr float kGlbVerticalScale = kViewerZPerSimUnit * 32.0f;

    // 30 degrees. The 3D camera's isometric pitch. Sprite quads use the cosine so a
    // face-on view keeps the source image's pixel aspect.
    constexpr float kIsometricPitchRadians = 0.5235987755982988f;
    constexpr float kIsometricPitchCosine = 0.8660254037844386f;

    // One sprite pixel across a vertical quad, in viewer units.
    constexpr float kSpritePixelAcross = kViewerZPerSimUnit * kIsometricPitchCosine;

    constexpr float kQuarterTurnRadians = 1.5707963267948966f;

    struct Vec3
    {
        float x;
        float y;
        float z;
    };

    // glTF is Y-up and right-handed. The viewer is Z-up, +X east, +Y north.
    // x = gx, y = -gz, z = gy, then Z is multiplied by kGlbVerticalScale.
    constexpr Vec3 gltfToViewer(float gx, float gy, float gz)
    {
        return { gx, -gz, gy * kGlbVerticalScale };
    }

    // Quarter-tile paint anchors at view rotation 0, in sim units from the tile corner.
    // Paint.SmallScenery.cpp takes SceneryQuadrantOffsets and subtracts 1 (the same
    // nudge as a full tile's 16,16 -> 15,15; see OpenRCT2 issue 17616). The table is
    // {8,8}, {8,24}, {24,24}, {24,8}, so the painted points are:
    //   0 (7, 7), 1 (7, 23), 2 (23, 23), 3 (23, 7).
    // The painter also adds the viewport rotation before indexing. The 3D view uses
    // the element's stored quadrant and does not. vOffsetCentre changes only the
    // full-tile paint offset and is not applied here.
    constexpr int32_t kQuarterTileSimX[4] = { 7, 7, 23, 23 };
    constexpr int32_t kQuarterTileSimY[4] = { 7, 23, 23, 7 };

    // Full-tile objects, including half and three-quarter footprints, stay on the
    // tile centre (16, 16 sim). Those footprints' 2D image offset of (3, 3) is deferred.
    inline void sceneryAnchor(int32_t tileX, int32_t tileY, bool fullTile, uint8_t quadrant, float& x, float& y)
    {
        x = static_cast<float>(tileX);
        y = static_cast<float>(tileY);
        if (fullTile)
        {
            x += 0.5f;
            y += 0.5f;
            return;
        }
        const uint8_t index = quadrant & 3;
        x += static_cast<float>(kQuarterTileSimX[index]) / 32.0f;
        y += static_cast<float>(kQuarterTileSimY[index]) / 32.0f;
    }

    // Small-scenery direction 0 is the authored facing. Each step is a clockwise
    // quarter turn about +Z, matching CoordsXY::rotate. The mesh origin is the pivot,
    // so a quarter-tile instance placed by sceneryAnchor rotates about that quadrant.
    constexpr float modelYawRadians(uint8_t direction)
    {
        return -static_cast<float>(direction & 3u) * kQuarterTurnRadians;
    }

    inline void rotateYaw(float yaw, float x, float y, float& outX, float& outY)
    {
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        outX = c * x - s * y;
        outY = s * x + c * y;
    }

    static_assert(viewerZ(16) == kLandStepHeight);
    static_assert(viewerZ(32) == 1.0f);
    static_assert(kGlbVerticalScale == 1.0f);
} // namespace OpenRCT2::View3D
