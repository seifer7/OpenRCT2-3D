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

namespace OpenRCT2::View3D
{
    // Matches Drawing::BGRAColour so the paletted UI can be expanded without including the game headers here.
    struct ColourBGRA
    {
        uint8_t blue;
        uint8_t green;
        uint8_t red;
        uint8_t alpha;
    };

    // The View menu toggle. While this is set, the main park viewport is a hole and bgfx presents the frame.
    bool isActive();
    void toggle();

    // Free camera while the 3D view is on. Right-button drag looks around; Shift slides the camera with
    // the drag, and Ctrl dollies.
    // Scroll steps follow the game wheel (negative moves the camera forward). Quarter turns are clockwise
    // when direction is positive. Scroll Map Up, Down, Left, and Right slide the camera on the next frame.
    // Those shortcuts keep the current rotation and height.
    void cameraDrag(int32_t dx, int32_t dy, bool shift, bool ctrl);
    void cameraScroll(int32_t wheelSteps);
    void cameraTurnQuarter(int32_t direction);
    void cameraSetMovement(bool forward, bool back, bool left, bool right);

    // True after bgfx has created a device. Cheap to call when the device is down.
    bool hasDevice();

    // True after bgfx refused to start. Stays set until shutdown(), so the game can keep the original present path.
    bool hasFailed();

    // Drops the bgfx device. Does nothing when no device exists.
    void shutdown();

    // Draws the paletted UI over the window. When world is true, index 0 is discarded and the 3D park is drawn
    // behind it. When world is false, the indexed image is opaque so the original 2D frame can stay on a device
    // that was created for the 3D view. glContext is null when bgfx should create the context, or an existing GL
    // context that is current on this thread. Returns false if the frame was not presented.
    bool present(
        void* sdlWindow, const uint8_t* indexed, int32_t width, int32_t height, int32_t pitch, const ColourBGRA* palette,
        void* glContext, bool world);

    // Draws the 3D park and the paletted UI into rgba (width * height * 4, R, G, B, A, top row first).
    // bgfx keeps a private GL context, so the game window's existing presenter stays in place. Returns
    // false until a frame is ready or the device cannot start.
    bool capture(
        void* sdlWindow, const uint8_t* indexed, int32_t width, int32_t height, int32_t pitch, const ColourBGRA* palette,
        uint8_t* rgba);

    // Draws the 3D park into rgba (width * height * 4, top row first) with the caller's GL context left
    // current. bgfx borrows that context; it does not create a second one on the window. GL state afterwards
    // belongs to bgfx, so the caller resets it before drawing again. Returns false when the frame was not drawn.
    bool renderPark(void* sdlWindow, int32_t width, int32_t height, uint8_t* rgba);
} // namespace OpenRCT2::View3D
