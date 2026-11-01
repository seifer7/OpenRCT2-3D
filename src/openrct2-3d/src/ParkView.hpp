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

namespace OpenRCT2::View3D::Park
{
    // Drops terrain meshes, the cached terrain images, and the free camera. Safe before bgfx::shutdown.
    void release();

    // Next draw places the camera on the main viewport: same centre, 2:1 pitch, rotation, and tile size.
    // Called both when the 3D view turns on and when it turns off.
    void matchMainView();

    // Right-button drag. Shift slides the camera with the cursor, Ctrl moves along the view, and a plain drag looks around.
    void cameraDrag(int32_t dx, int32_t dy, bool shift, bool ctrl);

    // Mouse wheel. A negative step is a scroll away from the user and moves the camera forward.
    void cameraScroll(int32_t wheelSteps);

    // Turns the camera about its vertical axis. Positive is clockwise, one unit is 90 degrees.
    void cameraTurnQuarter(int32_t direction);

    // Scroll Map held state. Applied on the next draw, along the current view, without turning or changing height.
    void cameraSetMovement(bool forward, bool back, bool left, bool right);

    // Draws terrain, water, path tiles, and small scenery into view 0. Surface and water images are
    // copied from the graphics the 2D view has loaded, once, when the 3D view starts. The mesh is
    // rebuilt when the land or the paths change. Small scenery uses a GLB when one is installed for
    // that object, and the original direction sprites otherwise. Returns false when the terrain
    // shaders are unavailable; the caller still clears the sky and draws the UI.
    bool draw(uint16_t backWidth, uint16_t backHeight);
} // namespace OpenRCT2::View3D::Park
