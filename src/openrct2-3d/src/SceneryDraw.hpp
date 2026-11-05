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

    // True when another sprite would pass the sprite share of the atlas or the handle cap.
    inline bool spriteOverShare(
        uint32_t count, uint32_t maxCount, uint32_t texels, uint32_t maxTexels, uint32_t dedicated, uint32_t maxDedicated)
    {
        return count >= maxCount || texels > maxTexels || dedicated > maxDedicated;
    }

    // Oldest sprite that was not drawn this frame. `requireStale` keeps sprites newer than
    // `staleFrames`. Returns -1 when every candidate was drawn this frame or none are stale.
    inline int32_t pickSpriteToDrop(
        const uint64_t* lastUsed, const uint8_t* usedThisFrame, int32_t count, uint64_t frame, uint32_t staleFrames,
        bool requireStale)
    {
        int32_t best = -1;
        uint64_t bestUsed = ~uint64_t{ 0 };
        if (lastUsed == nullptr || usedThisFrame == nullptr || count <= 0)
        {
            return -1;
        }
        for (int32_t i = 0; i < count; i++)
        {
            if (usedThisFrame[i] != 0)
            {
                continue;
            }
            const uint64_t age = frame >= lastUsed[i] ? frame - lastUsed[i] : 0;
            if (requireStale && age < staleFrames)
            {
                continue;
            }
            if (best < 0 || lastUsed[i] < bestUsed)
            {
                best = i;
                bestUsed = lastUsed[i];
            }
        }
        return best;
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
    struct SceneryFrameStats
    {
        int32_t placements = 0;
        int32_t modelInstances = 0;
        int32_t spriteInstances = 0;
        int32_t modelSubmits = 0;
        int32_t spriteSubmits = 0;
        int32_t distantSpriteSubmits = 0;
        int32_t distantSpriteInstances = 0;
    };

    // Pixels for a face when g1 has no element. The game leaves this null.
    using ScenerySpriteOverride = bool (*)(int32_t& width, int32_t& height, const uint8_t*& rgba);
    void setScenerySpriteOverride(ScenerySpriteOverride fn);

    void resetSceneryFrameStats();
    SceneryFrameStats sceneryFrameStats();
    // The next draw rebuilds every chunk. Park load already does this when it is not headless.
    void markAllSceneryDirty();

    // Filament's no-op device lets tests run uploads and submissions without a window or GPU.
    bool initSceneryTestDevice(uint16_t width, uint16_t height);
    void sceneryTestFrame();
    void shutdownSceneryTestDevice();

    // Drops scenery meshes and sprite textures before Filament shutdown.
    void releaseScenery();

    // Small scenery. An object with a GLB is instanced from that mesh. Anything
    // else is four original direction sprites on two crossed quads. Added to the
    // Filament scene, so the caller sets the camera first.
    void drawScenery();
} // namespace OpenRCT2::View3D::Park
