/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "View3D.h"

// Desktop builds take these symbols from src/openrct2-3d. Android and Emscripten keep the 2D renderer only.
#ifndef OPENRCT2_HAVE_VIEW3D

namespace OpenRCT2::View3D
{
    bool isActive()
    {
        return false;
    }

    void toggle()
    {
    }

    void cameraDrag(int32_t, int32_t, bool, bool)
    {
    }

    void cameraScroll(int32_t)
    {
    }

    void cameraTurnQuarter(int32_t)
    {
    }

    void cameraSetMovement(bool, bool, bool, bool)
    {
    }

    bool hasDevice()
    {
        return false;
    }

    bool hasFailed()
    {
        return false;
    }

    void shutdown()
    {
    }

    bool present(
        [[maybe_unused]] void* sdlWindow, [[maybe_unused]] const uint8_t* indexed, [[maybe_unused]] int32_t width,
        [[maybe_unused]] int32_t height, [[maybe_unused]] int32_t pitch, [[maybe_unused]] const ColourBGRA* palette,
        [[maybe_unused]] void* glContext, [[maybe_unused]] bool world)
    {
        return false;
    }

    bool capture(
        [[maybe_unused]] void* sdlWindow, [[maybe_unused]] const uint8_t* indexed, [[maybe_unused]] int32_t width,
        [[maybe_unused]] int32_t height, [[maybe_unused]] int32_t pitch, [[maybe_unused]] const ColourBGRA* palette,
        [[maybe_unused]] uint8_t* rgba)
    {
        return false;
    }
} // namespace OpenRCT2::View3D

#endif
