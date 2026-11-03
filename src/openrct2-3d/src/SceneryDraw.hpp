/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <openrct2/drawing/ImageId.hpp>

namespace OpenRCT2::View3D
{
    // Colours from the small-scenery element, applied to the sprite the 3D view draws.
    // A ghost remap is left alone.
    inline ImageId sceneryRemapImage(
        ImageId image, bool hasPrimary, uint8_t primary, bool hasSecondary, uint8_t secondary, bool hasTertiary,
        uint8_t tertiary)
    {
        if (image.IsRemap())
        {
            return image;
        }
        if (hasPrimary)
        {
            image = image.WithPrimary(static_cast<OpenRCT2::Drawing::Colour>(primary));
            if (hasSecondary)
            {
                image = image.WithSecondary(static_cast<OpenRCT2::Drawing::Colour>(secondary));
            }
        }
        if (hasTertiary)
        {
            image = image.WithTertiary(static_cast<OpenRCT2::Drawing::Colour>(tertiary));
        }
        return image;
    }

    // What one placement draws this frame. A resident model is the mesh. A model
    // that is still loading waits, so its sprite faces are not uploaded. A model
    // the GPU could not take (handle cap or a full atlas) and an object with no
    // model both draw the X-sprite. A failed upload is not a white mesh.
    enum class PlacementDraw : uint8_t
    {
        model,
        sprite,
        waiting,
    };

    inline PlacementDraw placementDraw(bool modelCandidate, bool resident, bool /*uploadFailed*/)
    {
        if (modelCandidate && resident)
        {
            return PlacementDraw::model;
        }
        // Loading, over budget, or a permanent failure is the X-sprite.
        return PlacementDraw::sprite;
    }

    // One held model may retry this frame. `keys` is sorted ascending and `due` is
    // parallel to it. `cursor` remembers the last key so the next frame continues
    // after it. A model that never fits therefore cannot take every upload slot.
    // Freed space retries one model that is not due yet, not the whole set.
    inline int32_t selectHeldRetry(const uint32_t* keys, const uint8_t* due, int32_t count, bool spaceFreed, uint32_t& cursor)
    {
        if (count <= 0 || keys == nullptr || due == nullptr)
        {
            return -1;
        }
        int32_t firstDue = -1;
        int32_t afterCursor = -1;
        for (int32_t i = 0; i < count; i++)
        {
            if (due[i] == 0)
            {
                continue;
            }
            if (firstDue < 0)
            {
                firstDue = i;
            }
            if (afterCursor < 0 && keys[i] > cursor)
            {
                afterCursor = i;
            }
        }
        int32_t chosen = afterCursor >= 0 ? afterCursor : firstDue;
        if (chosen < 0 && spaceFreed)
        {
            chosen = 0;
            for (int32_t i = 0; i < count; i++)
            {
                if (keys[i] > cursor)
                {
                    chosen = i;
                    break;
                }
            }
        }
        if (chosen < 0)
        {
            return -1;
        }
        cursor = keys[chosen];
        return chosen;
    }
} // namespace OpenRCT2::View3D

namespace OpenRCT2::View3D::Park
{
    // Drops scenery meshes and sprite textures. Safe before bgfx::shutdown.
    void releaseScenery();

    // Small scenery. An object with a GLB is instanced from that mesh. Anything
    // else is four original direction sprites on two crossed quads. Submitted to
    // view 0, so the caller sets the camera first.
    void drawScenery();
} // namespace OpenRCT2::View3D::Park
