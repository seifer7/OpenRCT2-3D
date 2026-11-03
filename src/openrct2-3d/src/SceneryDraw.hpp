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
} // namespace OpenRCT2::View3D

namespace OpenRCT2::View3D::Park
{
    struct SceneryFrameStats
    {
        int32_t placements = 0;
        int32_t modelInstances = 0;
        int32_t spriteInstances = 0;
        int32_t modelSubmits = 0;
        int32_t spriteSubmits = 0;
    };

    // Pixels for a face when g1 has no element. The game leaves this null.
    using ScenerySpriteOverride = bool (*)(int32_t& width, int32_t& height, const uint8_t*& rgba);
    void setScenerySpriteOverride(ScenerySpriteOverride fn);

    void resetSceneryFrameStats();
    SceneryFrameStats sceneryFrameStats();
    // The next draw rebuilds every chunk. Park load already does this when it is not headless.
    void markAllSceneryDirty();

    // Noop device so a test can run the real upload and submit path without a window.
    bool initSceneryTestDevice(uint16_t width, uint16_t height);
    void sceneryTestFrame();
    void shutdownSceneryTestDevice();

    // Drops scenery meshes and sprite textures. Safe before bgfx::shutdown.
    void releaseScenery();

    // Small scenery. An object with a GLB is instanced from that mesh. Anything
    // else is four original direction sprites on two crossed quads. Submitted to
    // view 0, so the caller sets the camera first.
    void drawScenery();
} // namespace OpenRCT2::View3D::Park
