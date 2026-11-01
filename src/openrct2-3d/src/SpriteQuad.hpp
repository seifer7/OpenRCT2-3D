/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "ViewerScale.hpp"

#include <cstdint>

namespace OpenRCT2::View3D
{
    // One face of the crossed-sprite fallback. Corners are counter-clockwise as
    // seen by the camera that face is built for: bottom-left, bottom-right,
    // top-right, top-left. The origin is the 2D sprite anchor.
    struct SpriteQuad
    {
        Vec3 corner[4];
        Vec3 normal;
    };

    // Face 0 faces the rotation-0 camera (north-east of the object, the same
    // corner ParkView::seedFlyCamera uses before it mirrors X). Faces 1..3 step
    // with CoordsXY{ 1, 1 }.rotate(-rotation).
    inline SpriteQuad makeDirectionSpriteQuad(int face, int16_t width, int16_t height, int16_t xOffset, int16_t yOffset)
    {
        // Unnormalised eye offsets. Each pair is a unit direction times sqrt(2).
        constexpr float kEyeX[4] = { 1.0f, -1.0f, -1.0f, 1.0f };
        constexpr float kEyeY[4] = { 1.0f, 1.0f, -1.0f, -1.0f };
        constexpr float kInvSqrt2 = 0.7071067811865476f;

        const int index = face & 3;
        SpriteQuad quad;
        quad.normal = { kEyeX[index] * kInvSqrt2, kEyeY[index] * kInvSqrt2, 0.0f };

        // look × up, with look = -normal and up = +Z. That is screen-right when
        // the camera sits on this normal and looks at the origin.
        const Vec3 side{ -quad.normal.y, quad.normal.x, 0.0f };

        const float left = static_cast<float>(xOffset) * kSpritePixelAcross;
        const float right = static_cast<float>(xOffset + width) * kSpritePixelAcross;
        // Screen Y grows downward and world Z grows up, and one pixel of sprite
        // height is one sim Z.
        const float top = -static_cast<float>(yOffset) * kViewerZPerSimUnit;
        const float bottom = -static_cast<float>(yOffset + height) * kViewerZPerSimUnit;

        const auto place = [&](float across, float up) { return Vec3{ side.x * across, side.y * across, up }; };
        quad.corner[0] = place(left, bottom);
        quad.corner[1] = place(right, bottom);
        quad.corner[2] = place(right, top);
        quad.corner[3] = place(left, top);
        return quad;
    }
} // namespace OpenRCT2::View3D
