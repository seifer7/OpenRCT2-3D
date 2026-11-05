/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <openrct2-ui/drawing/View3D.h>

#include "FilamentRenderer.hpp"
#include "PaletteComposite.hpp"
#include "ParkView.hpp"

#include <SDL.h>
#include <openrct2/drawing/Drawing.Screen.h>
#include <openrct2/interface/Viewport.h>

#include <cstdio>
#include <exception>

namespace OpenRCT2::View3D
{
    namespace
    {
        bool _active = false;
        bool _failed = false;

        // Filament owns a separate device and never adopts the game's GL context.
        struct RestoreGl
        {
            SDL_Window* window = SDL_GL_GetCurrentWindow();
            SDL_GLContext context = SDL_GL_GetCurrentContext();

            RestoreGl()
            {
                if (window != nullptr && context != nullptr)
                {
                    SDL_GL_MakeCurrent(window, nullptr);
                }
            }
            ~RestoreGl()
            {
                if (window != nullptr && context != nullptr && SDL_GL_MakeCurrent(window, context) != 0)
                {
                    std::fprintf(stderr, "View3D: unable to restore SDL context: %s\n", SDL_GetError());
                }
            }
        };

        void fail(const char* message)
        {
            if (!_failed)
            {
                std::fprintf(stderr, "View3D: Filament failed: %s; returning to 2D\n", message);
            }
            _failed = true;
            _active = false;
            ViewportSetMainScrollSuppressed(false);
            Park::release();
            Gpu::shutdown();
            Drawing::GfxInvalidateScreen();
        }
    }

    bool isActive()
    {
        return _active;
    }

    void toggle()
    {
        _active = !_active;
        if (_active)
        {
            _failed = false;
        }
        ViewportSetMainScrollSuppressed(_active);
        Park::matchMainView();
        Drawing::GfxInvalidateScreen();
    }

    void cameraDrag(int32_t dx, int32_t dy, bool shift, bool ctrl)
    {
        Park::cameraDrag(dx, dy, shift, ctrl);
    }

    void cameraScroll(int32_t wheelSteps)
    {
        Park::cameraScroll(wheelSteps);
    }

    void cameraTurnQuarter(int32_t direction)
    {
        Park::cameraTurnQuarter(direction);
    }

    void cameraSetMovement(bool forward, bool back, bool left, bool right)
    {
        Park::cameraSetMovement(forward, back, left, right);
    }

    bool hasDevice()
    {
        return Gpu::ready();
    }

    bool hasFailed()
    {
        return _failed;
    }

    void shutdown()
    {
        RestoreGl restore;
        Park::release();
        Gpu::shutdown();
        _failed = false;
        ViewportSetMainScrollSuppressed(_active);
    }

    bool renderPark(void* sdlWindow, int32_t width, int32_t height, uint8_t* rgba)
    {
        if (_failed || sdlWindow == nullptr || rgba == nullptr || width <= 0 || height <= 0
            || width > UINT16_MAX || height > UINT16_MAX)
        {
            return false;
        }
        RestoreGl restore;
        try
        {
            if (!Gpu::initialize())
            {
                fail("device initialization");
                return false;
            }
            Gpu::beginScene();
            if (!Park::draw(static_cast<uint16_t>(width), static_cast<uint16_t>(height)))
            {
                fail("park scene resources");
                return false;
            }
            return Gpu::render(static_cast<uint32_t>(width), static_cast<uint32_t>(height), rgba);
        }
        catch (const std::exception& error)
        {
            fail(error.what());
            return false;
        }
    }

    bool capture(void* sdlWindow, const uint8_t* indexed, int32_t width, int32_t height, int32_t pitch,
        const ColourBGRA* palette, uint8_t* rgba)
    {
        if (indexed == nullptr || palette == nullptr || pitch < width || !renderPark(sdlWindow, width, height, rgba))
        {
            return false;
        }
        return compositeIndexedUi(indexed, width, height, pitch, palette, rgba);
    }
}
