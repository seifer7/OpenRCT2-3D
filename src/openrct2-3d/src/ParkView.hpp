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
    // Drops terrain meshes, the cached terrain images, and the free camera before Filament shutdown.
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

    // Adds terrain, water, path tiles, and small scenery to the Filament scene. Surface and water images are
    // copied from the graphics the 2D view has loaded, once, when the 3D view starts. The mesh is
    // rebuilt when the land or the paths change. Small scenery uses a GLB when one is installed for
    // that object, and the original direction sprites otherwise. Returns false when the terrain
    // materials are unavailable; the caller can return to the original 2D view.
    bool draw(uint16_t backWidth, uint16_t backHeight);

    // The pose lookAtMainView takes on the first frame: map centre, zoom 0, this back-buffer size.
    // Does not submit the view. copyCamera is valid afterwards.
    void poseDefaultCamera(uint16_t backWidth, uint16_t backHeight);

    // Same commit as the in-game view, aimed at targetX/targetY, with tilePixels as the 2D zoom
    // (64 at zoom 0, 8 at zoom 3). copyCamera is valid afterwards.
    void poseIsoCamera(uint16_t backWidth, uint16_t backHeight, float tilePixels, float targetX, float targetY);

    // Same matrices as the in-game view for a frame of this height and a zoom expressed as pixels per tile.
    // The target is the origin. Restores the live camera when it returns.
    void sceneryClipForFrame(
        float frameHeight, float tilePixels, float aspect, float viewProj[16], float& eyeX, float& eyeY, float& eyeZ,
        float& focusX, float& focusY);

    // Eye position and the column-major view-projection from the last draw. False before the first frame.
    bool copyCamera(float& eyeX, float& eyeY, float& eyeZ, float viewProj[16]);
    // Ground point the camera is aimed at. False before the first frame.
    bool copyViewFocus(float& focusX, float& focusY);
} // namespace OpenRCT2::View3D::Park
