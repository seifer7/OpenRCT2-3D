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

    // The View menu toggle. While this is set, the main park viewport reveals the Filament image.
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

    // True after Filament has created a device. Cheap to call when the device is down.
    bool hasDevice();

    // True after Filament failed. Cleared by shutdown() or another attempt to enable the 3D view.
    bool hasFailed();

    // Drops the Filament device. Does nothing when no device exists.
    void shutdown();

    // Draws the 3D park and the paletted UI into rgba (width * height * 4, R, G, B, A, top row first).
    // Filament owns an independent offscreen target; the game's presenter stays in place.
    // Palette index 0 reveals the park. Returns false until a complete frame is available.
    bool capture(
        void* sdlWindow, const uint8_t* indexed, int32_t width, int32_t height, int32_t pitch, const ColourBGRA* palette,
        uint8_t* rgba);

    // Draws the 3D park into rgba (width * height * 4, top row first) with the caller's GL context left
    // current. Filament never adopts or presents the game window's context. Returns false when the frame was not drawn.
    bool renderPark(void* sdlWindow, int32_t width, int32_t height, uint8_t* rgba);
} // namespace OpenRCT2::View3D
