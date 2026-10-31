/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <openrct2-ui/drawing/View3D.h>

// clang-format off
#include <bgfx/bgfx.h>
#include <SDL.h>
#include <SDL_syswm.h>
// clang-format on

// X11's None collides with names in the headers above.
#ifdef None
    #undef None
#endif

#include "ParkView.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <openrct2/interface/Viewport.h>
#include <vector>

#if !defined(_WIN32)
    #include <dlfcn.h>
#endif

namespace OpenRCT2::View3D
{
    namespace
    {
        constexpr uint32_t kBlankClear = 0x6FA8DCff;
        constexpr uint32_t kVaryingHash = 0x3D563377;
        constexpr uint8_t kShaderVersion = 0x0c;
        constexpr const char kSamplerName[] = "s_texColor";

        // v = 0 is the first uploaded row. bgfx treats that as the top of the image, and OpenGL's y = 1 is the top of the
        // window.
        constexpr float kUiTriangle[] = {
            -1.0f, -1.0f, 0.0f, 1.0f, 3.0f, -1.0f, 2.0f, 1.0f, -1.0f, 3.0f, 0.0f, -1.0f,
        };

        constexpr const char kVertexShader[] = "in vec2 a_position;\n"
                                               "in vec2 a_texcoord0;\n"
                                               "out vec2 v_texcoord0;\n"
                                               "void main()\n"
                                               "{\n"
                                               "    gl_Position = vec4(a_position.xy, 0.0, 1.0);\n"
                                               "    v_texcoord0 = a_texcoord0;\n"
                                               "}\n";

        constexpr const char kFragmentShader[] = "uniform sampler2D s_texColor;\n"
                                                 "in vec2 v_texcoord0;\n"
                                                 "layout(location = 0) out vec4 bgfx_FragColor;\n"
                                                 "void main()\n"
                                                 "{\n"
                                                 "    vec4 colour = texture(s_texColor, v_texcoord0);\n"
                                                 "    if (colour.w < 0.00392156862745098)\n"
                                                 "    {\n"
                                                 "        discard;\n"
                                                 "    }\n"
                                                 "    bgfx_FragColor = colour;\n"
                                                 "}\n";

        bool _active = false;
        bool _device = false;
        bool _failed = false;
        bool _loggedFailure = false;

        void* _glContext = nullptr;
        bool _offscreen = false;
        uint16_t _backWidth = 0;
        uint16_t _backHeight = 0;
        uint16_t _textureWidth = 0;
        uint16_t _textureHeight = 0;
        uint16_t _frameWidth = 0;
        uint16_t _frameHeight = 0;

        bgfx::ProgramHandle _program = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle _sampler = BGFX_INVALID_HANDLE;
        bgfx::VertexBufferHandle _vertices = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle _texture = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle _frame = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle _readback = BGFX_INVALID_HANDLE;
        std::vector<uint8_t> _rgba;
        std::vector<uint8_t> _readPixels;

        void logFailure(const char* message)
        {
            if (_loggedFailure)
            {
                return;
            }
            _loggedFailure = true;
            std::fprintf(stderr, "View3D: %s\n", message);
        }

        void appendU8(std::vector<uint8_t>& out, uint8_t value)
        {
            out.push_back(value);
        }

        void appendU16(std::vector<uint8_t>& out, uint16_t value)
        {
            out.push_back(static_cast<uint8_t>(value));
            out.push_back(static_cast<uint8_t>(value >> 8));
        }

        void appendU32(std::vector<uint8_t>& out, uint32_t value)
        {
            out.push_back(static_cast<uint8_t>(value));
            out.push_back(static_cast<uint8_t>(value >> 8));
            out.push_back(static_cast<uint8_t>(value >> 16));
            out.push_back(static_cast<uint8_t>(value >> 24));
        }

        void appendShaderSource(std::vector<uint8_t>& out, const char* source)
        {
            const auto size = static_cast<uint32_t>(std::strlen(source) + 1);
            appendU32(out, size);
            out.insert(out.end(), source, source + size);
        }

        std::vector<uint8_t> packShader(char stage, uint32_t hashIn, uint32_t hashOut, bool withSampler, const char* source)
        {
            std::vector<uint8_t> out;
            appendU8(out, static_cast<uint8_t>(stage));
            appendU8(out, static_cast<uint8_t>('S'));
            appendU8(out, static_cast<uint8_t>('H'));
            appendU8(out, kShaderVersion);
            appendU32(out, hashIn);
            appendU32(out, hashOut);
            appendU32(out, 0);
            appendU32(out, 0);
            appendU16(out, withSampler ? 1 : 0);
            if (withSampler)
            {
                const auto nameSize = static_cast<uint8_t>(std::strlen(kSamplerName));
                appendU8(out, nameSize);
                out.insert(out.end(), kSamplerName, kSamplerName + nameSize);
                appendU8(out, static_cast<uint8_t>(bgfx::UniformType::Sampler));
                appendU8(out, 1);
                appendU16(out, 0);
                appendU16(out, 1);
                appendU8(out, 0);
                appendU8(out, 2);
                appendU16(out, 0);
            }
            appendShaderSource(out, source);
            return out;
        }

        bgfx::ShaderHandle createPackedShader(
            char stage, uint32_t hashIn, uint32_t hashOut, bool withSampler, const char* source)
        {
            const std::vector<uint8_t> packed = packShader(stage, hashIn, hashOut, withSampler, source);
            const bgfx::Memory* memory = bgfx::copy(packed.data(), static_cast<uint32_t>(packed.size()));
            return bgfx::createShader(memory);
        }

        bool nativeWindow(SDL_Window* window, void*& nwh, void*& ndt)
        {
            SDL_SysWMinfo info;
            SDL_VERSION(&info.version);
            if (SDL_GetWindowWMInfo(window, &info) != SDL_TRUE)
            {
                logFailure("SDL_GetWindowWMInfo failed");
                return false;
            }

            nwh = nullptr;
            ndt = nullptr;
#if defined(_WIN32)
            if (info.subsystem != SDL_SYSWM_WINDOWS)
            {
                logFailure("3D view needs a Win32 window");
                return false;
            }
            nwh = info.info.win.window;
#elif defined(__APPLE__)
            if (info.subsystem != SDL_SYSWM_COCOA)
            {
                logFailure("3D view needs a Cocoa window");
                return false;
            }
            nwh = info.info.cocoa.window;
#else
            if (info.subsystem != SDL_SYSWM_X11)
            {
                logFailure("3D view needs an X11 window");
                return false;
            }
            nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(info.info.x11.window));
            ndt = info.info.x11.display;
#endif
            return nwh != nullptr;
        }

        bool windowPixels(SDL_Window* window, uint16_t& width, uint16_t& height)
        {
            int pixelWidth = 0;
            int pixelHeight = 0;
#if SDL_VERSION_ATLEAST(2, 26, 0)
            SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
#else
            SDL_GetWindowSize(window, &pixelWidth, &pixelHeight);
#endif
            if (pixelWidth <= 0 || pixelHeight <= 0 || pixelWidth > UINT16_MAX || pixelHeight > UINT16_MAX)
            {
                return false;
            }
            width = static_cast<uint16_t>(pixelWidth);
            height = static_cast<uint16_t>(pixelHeight);
            return true;
        }

        void destroyDevice()
        {
            if (!_device)
            {
                return;
            }

            Park::release();

            if (bgfx::isValid(_frame))
            {
                bgfx::destroy(_frame);
            }
            if (bgfx::isValid(_readback))
            {
                bgfx::destroy(_readback);
            }
            if (bgfx::isValid(_texture))
            {
                bgfx::destroy(_texture);
            }
            if (bgfx::isValid(_vertices))
            {
                bgfx::destroy(_vertices);
            }
            if (bgfx::isValid(_program))
            {
                bgfx::destroy(_program);
            }
            if (bgfx::isValid(_sampler))
            {
                bgfx::destroy(_sampler);
            }
            _frame = BGFX_INVALID_HANDLE;
            _readback = BGFX_INVALID_HANDLE;
            _texture = BGFX_INVALID_HANDLE;
            _vertices = BGFX_INVALID_HANDLE;
            _program = BGFX_INVALID_HANDLE;
            _sampler = BGFX_INVALID_HANDLE;
            _textureWidth = 0;
            _textureHeight = 0;
            _frameWidth = 0;
            _frameHeight = 0;
            _backWidth = 0;
            _backHeight = 0;
            bgfx::shutdown();
            _device = false;
            _offscreen = false;
            _glContext = nullptr;
        }

        bool createCompositeResources()
        {
            _sampler = bgfx::createUniform(kSamplerName, bgfx::UniformType::Sampler);
            const bgfx::ShaderHandle vertex = createPackedShader('V', 0, kVaryingHash, false, kVertexShader);
            const bgfx::ShaderHandle fragment = createPackedShader('F', kVaryingHash, 0, true, kFragmentShader);
            _program = bgfx::createProgram(vertex, fragment, true);
            if (!bgfx::isValid(_sampler) || !bgfx::isValid(_program))
            {
                logFailure("UI composite shader failed");
                bgfx::shutdown();
                return false;
            }

            bgfx::VertexLayout layout;
            layout.begin(bgfx::RendererType::OpenGL)
                .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
                .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
                .end();
            _vertices = bgfx::createVertexBuffer(bgfx::copy(kUiTriangle, sizeof(kUiTriangle)), layout);
            if (!bgfx::isValid(_vertices))
            {
                logFailure("UI composite vertices failed");
                bgfx::shutdown();
                return false;
            }
            return true;
        }

        // renderFrame() before init keeps bgfx on this thread. A failed init clears that latch, so call it again
        // before every attempt.
        bool initDevice(const bgfx::Init& init, void* glContext, bool offscreen, uint16_t backWidth, uint16_t backHeight)
        {
            bgfx::renderFrame();
            if (!bgfx::init(init))
            {
                return false;
            }
            if (!createCompositeResources())
            {
                _failed = true;
                return false;
            }

            _device = true;
            _offscreen = offscreen;
            _glContext = glContext;
            _backWidth = backWidth;
            _backHeight = backHeight;
            std::fprintf(
                stderr, "View3D: 3D view on %s%s\n", bgfx::getRendererName(bgfx::getRendererType()),
                offscreen ? ", copied into the original display" : "");
            return true;
        }

        bool createDevice(SDL_Window* window, void* glContext, uint16_t backWidth, uint16_t backHeight)
        {
            void* nwh = nullptr;
            void* ndt = nullptr;
            if (!nativeWindow(window, nwh, ndt))
            {
                _failed = true;
                return false;
            }

            bgfx::Init init;
            init.type = bgfx::RendererType::OpenGL;
            init.fallback = false;
            init.platformData.context = glContext;
            init.swapChain.nwh = nwh;
            init.swapChain.ndt = ndt;
            init.swapChain.width = backWidth;
            init.swapChain.height = backHeight;
            init.reset = BGFX_RESET_VSYNC;
            if (!initDevice(init, glContext, false, backWidth, backHeight))
            {
                if (!_failed && glContext == nullptr)
                {
                    logFailure("bgfx init failed");
                    _failed = true;
                }
                return false;
            }
            return true;
        }

        bool ensureDevice(SDL_Window* window, void* glContext, uint16_t backWidth, uint16_t backHeight)
        {
            if (_failed)
            {
                return false;
            }
            // A capture device draws into a texture. It is not this window's swap chain.
            // A context borrowed from the drawing engine cannot present this window on its own.
            if (_device && (_offscreen || (_glContext != nullptr && glContext != _glContext)))
            {
                destroyDevice();
            }
            if (!_device && !createDevice(window, glContext, backWidth, backHeight))
            {
                return false;
            }

            if (_backWidth != backWidth || _backHeight != backHeight)
            {
                void* nwh = nullptr;
                void* ndt = nullptr;
                if (!nativeWindow(window, nwh, ndt))
                {
                    _failed = true;
                    destroyDevice();
                    return false;
                }
                bgfx::SwapChain chain;
                chain.nwh = nwh;
                chain.ndt = ndt;
                chain.width = backWidth;
                chain.height = backHeight;
                bgfx::reset(BGFX_RESET_VSYNC, &chain);
                _backWidth = backWidth;
                _backHeight = backHeight;
            }
            return true;
        }

        // The OpenGL drawing engine keeps its own context current. bgfx draws the park with that context
        // and must not replace the window's swap chain, so a resize leaves the surface alone.
        bool ensureSharedDevice(SDL_Window* window, void* glContext, uint16_t backWidth, uint16_t backHeight)
        {
            if (_failed)
            {
                return false;
            }
            if (_device && (_offscreen || _glContext != glContext))
            {
                destroyDevice();
            }
            if (_device)
            {
                return true;
            }
            if (!createDevice(window, glContext, backWidth, backHeight))
            {
                if (!_failed)
                {
                    logFailure("bgfx init failed");
                    _failed = true;
                }
                return false;
            }
            std::fprintf(stderr, "View3D: using the drawing engine OpenGL context\n");
            return true;
        }

        bool ensureTexture(uint16_t width, uint16_t height)
        {
            if (bgfx::isValid(_texture) && (_textureWidth != width || _textureHeight != height))
            {
                bgfx::destroy(_texture);
                _texture = BGFX_INVALID_HANDLE;
            }
            if (bgfx::isValid(_texture))
            {
                return true;
            }

            _texture = bgfx::createTexture2D(
                width, height, false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            if (!bgfx::isValid(_texture))
            {
                logFailure("UI texture allocation failed");
                _failed = true;
                destroyDevice();
                return false;
            }
            _textureWidth = width;
            _textureHeight = height;
            return true;
        }

        bool ensureOffscreen(SDL_Window* gameWindow)
        {
            if (_failed)
            {
                return false;
            }
            if (_device && !_offscreen)
            {
                destroyDevice();
            }
            if (_device)
            {
                return true;
            }

            // No window and a 0x0 back buffer. bgfx then owns a private GL context (a dummy window on
            // Windows, a pbuffer on Linux) and never replaces the presenter already on the game window.
            // Init() defaults the back buffer to 1280x720, and headless mode rejects any non-zero size.
            bgfx::Init init;
            init.type = bgfx::RendererType::OpenGL;
            init.fallback = false;
            init.reset = BGFX_RESET_NONE;
            init.swapChain.width = 0;
            init.swapChain.height = 0;
            if (initDevice(init, nullptr, true, 0, 0))
            {
                return true;
            }
            if (_failed)
            {
                return false;
            }

            // Do not create a second GL context on the game window. That swap replaces the picture
            // with black when the window already has the drawing engine's context.
            (void)gameWindow;
            if (!_failed)
            {
                logFailure("bgfx init failed");
                _failed = true;
            }
            return false;
        }

        void destroyFrameTargets()
        {
            if (bgfx::isValid(_frame))
            {
                bgfx::destroy(_frame);
            }
            if (bgfx::isValid(_readback))
            {
                bgfx::destroy(_readback);
            }
            _frame = BGFX_INVALID_HANDLE;
            _readback = BGFX_INVALID_HANDLE;
            _frameWidth = 0;
            _frameHeight = 0;
        }

        bool ensureFrameTargets(uint16_t width, uint16_t height)
        {
            if (bgfx::isValid(_frame) && bgfx::isValid(_readback) && _frameWidth == width && _frameHeight == height)
            {
                return true;
            }
            destroyFrameTargets();

            // The colour-only helper does not attach depth, so a later blended draw (water) paints over
            // whatever is already in the colour target. Depth is a write-only renderbuffer: a depth
            // texture is incomplete on some OpenGL drivers, and then the depth test never rejects.
            const uint64_t colourFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
            const uint64_t depthFlags = BGFX_TEXTURE_RT_WRITE_ONLY;
            const bgfx::TextureHandle colour = bgfx::createTexture2D(
                width, height, false, 1, bgfx::TextureFormat::RGBA8, colourFlags);
            bgfx::TextureHandle depth = BGFX_INVALID_HANDLE;
            // Depth-only first. A sampled depth texture is what some drivers leave incomplete.
            const bgfx::TextureFormat::Enum depthFormats[] = {
                bgfx::TextureFormat::D24,
                bgfx::TextureFormat::D32F,
                bgfx::TextureFormat::D16,
                bgfx::TextureFormat::D24S8,
            };
            const char* depthName = "none";
            for (const bgfx::TextureFormat::Enum format : depthFormats)
            {
                if (!bgfx::isTextureValid(0, false, 1, format, depthFlags))
                {
                    continue;
                }
                depth = bgfx::createTexture2D(width, height, false, 1, format, depthFlags);
                if (bgfx::isValid(depth))
                {
                    switch (format)
                    {
                        case bgfx::TextureFormat::D24:
                            depthName = "D24";
                            break;
                        case bgfx::TextureFormat::D32F:
                            depthName = "D32F";
                            break;
                        case bgfx::TextureFormat::D16:
                            depthName = "D16";
                            break;
                        case bgfx::TextureFormat::D24S8:
                            depthName = "D24S8";
                            break;
                        default:
                            break;
                    }
                    break;
                }
            }
            static bool loggedDepth = false;
            if (!loggedDepth)
            {
                loggedDepth = true;
                std::fprintf(stderr, "View3D: park depth attachment is a %s renderbuffer\n", depthName);
            }
            if (bgfx::isValid(colour) && bgfx::isValid(depth))
            {
                const bgfx::TextureHandle attachments[] = { colour, depth };
                // true hands both textures to the framebuffer, including when creation fails.
                _frame = bgfx::createFrameBuffer(2, attachments, true);
            }
            else
            {
                if (bgfx::isValid(colour))
                {
                    bgfx::destroy(colour);
                }
                if (bgfx::isValid(depth))
                {
                    bgfx::destroy(depth);
                }
            }
            _readback = bgfx::createTexture2D(
                width, height, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
            if (!bgfx::isValid(_frame) || !bgfx::isValid(_readback))
            {
                logFailure("3D readback target failed");
                _failed = true;
                destroyDevice();
                return false;
            }
            _frameWidth = width;
            _frameHeight = height;
            return true;
        }

        void expandIndexed(
            const uint8_t* indexed, int32_t width, int32_t height, int32_t pitch, const ColourBGRA* palette, bool punchHole)
        {
            _rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
            uint8_t* dst = _rgba.data();
            for (int32_t y = 0; y < height; y++)
            {
                const uint8_t* row = indexed + static_cast<size_t>(y) * static_cast<size_t>(pitch);
                for (int32_t x = 0; x < width; x++)
                {
                    const uint8_t index = row[x];
                    const ColourBGRA colour = palette[index];
                    dst[0] = colour.red;
                    dst[1] = colour.green;
                    dst[2] = colour.blue;
                    dst[3] = punchHole && index == 0 ? 0 : 255;
                    dst += 4;
                }
            }
        }
    } // namespace

    bool isActive()
    {
        return _active;
    }

    void toggle()
    {
        _active = !_active;
        ViewportSetMainScrollSuppressed(_active);
        // Arm on the way out as well. The next park draw is the moment 3D is visible again, and the 2D
        // view may have moved in between.
        Park::matchMainView();
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
        return _device;
    }

    bool hasFailed()
    {
        return _failed;
    }

    void shutdown()
    {
        _failed = false;
        _loggedFailure = false;
        // The drawing engine can be recreated while the 3D view stays on. Keep the scroll lock in step with it.
        ViewportSetMainScrollSuppressed(_active);
        destroyDevice();
    }

    bool present(
        void* sdlWindow, const uint8_t* indexed, int32_t width, int32_t height, int32_t pitch, const ColourBGRA* palette,
        void* glContext, bool world)
    {
        auto* window = static_cast<SDL_Window*>(sdlWindow);
        if (window == nullptr || indexed == nullptr || palette == nullptr || width <= 0 || height <= 0 || pitch < width
            || width > UINT16_MAX || height > UINT16_MAX)
        {
            return false;
        }

        uint16_t backWidth = 0;
        uint16_t backHeight = 0;
        if (!windowPixels(window, backWidth, backHeight))
        {
            return false;
        }

        // A private bgfx context cannot be made current while SDL's context is. Release it for the frame
        // and put it back afterwards so the next UI draw still has its textures.
        struct ReleaseForeignGl
        {
            SDL_Window* window = nullptr;
            SDL_GLContext context = nullptr;
            bool active = false;
            explicit ReleaseForeignGl(bool release)
            {
                if (!release)
                {
                    return;
                }
                window = SDL_GL_GetCurrentWindow();
                context = SDL_GL_GetCurrentContext();
                if (window != nullptr && context != nullptr)
                {
                    SDL_GL_MakeCurrent(window, nullptr);
                    active = true;
                }
            }
            ~ReleaseForeignGl()
            {
                if (active)
                {
                    SDL_GL_MakeCurrent(window, context);
                }
            }
        } releaseForeignGl(glContext == nullptr || (_device && _glContext == nullptr));

        if (!ensureDevice(window, glContext, backWidth, backHeight))
        {
            return false;
        }
        if (!ensureTexture(static_cast<uint16_t>(width), static_cast<uint16_t>(height)))
        {
            return false;
        }

        expandIndexed(indexed, width, height, pitch, palette, world);
        const bgfx::Memory* pixels = bgfx::copy(_rgba.data(), static_cast<uint32_t>(_rgba.size()));
        bgfx::updateTexture2D(_texture, 0, 0, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height), pixels);

        bgfx::setViewRect(0, 0, 0, backWidth, backHeight);
        if (world)
        {
            bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, kBlankClear, 1.0f, 0);
            bgfx::touch(0);
            Park::draw(backWidth, backHeight);
        }
        else
        {
            // The clear flags stick until replaced. Leave the park view alone so it does not wipe the 2D frame.
            bgfx::setViewClear(0, BGFX_CLEAR_NONE, 0, 1.0f, 0);
            bgfx::touch(0);
        }

        bgfx::setViewRect(1, 0, 0, backWidth, backHeight);
        bgfx::setViewClear(1, BGFX_CLEAR_NONE, 0, 1.0f, 0);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::setTexture(0, _sampler, _texture);
        bgfx::setVertexBuffer(0, _vertices);
        bgfx::submit(1, _program);
        bgfx::frame();
        return true;
    }

    bool capture(
        void* sdlWindow, const uint8_t* indexed, int32_t width, int32_t height, int32_t pitch, const ColourBGRA* palette,
        uint8_t* rgba)
    {
        if (rgba == nullptr || indexed == nullptr || palette == nullptr || width <= 0 || height <= 0 || pitch < width
            || width > UINT16_MAX || height > UINT16_MAX)
        {
            return false;
        }

        // SDL's hardware display on Linux already has a GLX context current. bgfx uses EGL, and
        // eglMakeCurrent fails with EGL_BAD_ACCESS until that GLX context is released. Windows
        // Direct3D has no GL context here, so this is a no-op and the private WGL device is left alone.
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
                if (window == nullptr || context == nullptr)
                {
                    return;
                }
                // bgfx's private context on Linux is EGL. A GLX context cannot be made current again
                // until that EGL binding is released, and the next glGetError then never clears.
#if !defined(_WIN32)
                using ReleaseFn = unsigned int (*)();
                static ReleaseFn releaseThread = nullptr;
                static bool lookedUp = false;
                if (!lookedUp)
                {
                    lookedUp = true;
                    void* library = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_NOLOAD);
                    if (library == nullptr)
                    {
                        library = dlopen("libEGL.so.1", RTLD_LAZY);
                    }
                    if (library != nullptr)
                    {
                        releaseThread = reinterpret_cast<ReleaseFn>(dlsym(library, "eglReleaseThread"));
                    }
                }
                if (releaseThread != nullptr)
                {
                    releaseThread();
                }
#endif
                if (SDL_GL_MakeCurrent(window, context) != 0)
                {
                    std::fprintf(stderr, "View3D: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
                }
            }
        } restoreGl;

        const auto frameWidth = static_cast<uint16_t>(width);
        const auto frameHeight = static_cast<uint16_t>(height);
        if (!ensureOffscreen(static_cast<SDL_Window*>(sdlWindow)) || !ensureTexture(frameWidth, frameHeight)
            || !ensureFrameTargets(frameWidth, frameHeight))
        {
            return false;
        }

        expandIndexed(indexed, width, height, pitch, palette, true);
        const bgfx::Memory* pixels = bgfx::copy(_rgba.data(), static_cast<uint32_t>(_rgba.size()));
        bgfx::updateTexture2D(_texture, 0, 0, 0, 0, frameWidth, frameHeight, pixels);

        bgfx::setViewFrameBuffer(0, _frame);
        bgfx::setViewRect(0, 0, 0, frameWidth, frameHeight);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, kBlankClear, 1.0f, 0);
        bgfx::touch(0);
        Park::draw(frameWidth, frameHeight);

        bgfx::setViewFrameBuffer(1, _frame);
        bgfx::setViewRect(1, 0, 0, frameWidth, frameHeight);
        bgfx::setViewClear(1, BGFX_CLEAR_NONE, 0, 1.0f, 0);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::setTexture(0, _sampler, _texture);
        bgfx::setVertexBuffer(0, _vertices);
        bgfx::submit(1, _program);

        bgfx::TextureRegion source;
        source.init(bgfx::getTexture(_frame), 0, 0, frameWidth, frameHeight);
        bgfx::TextureRegion destination;
        destination.init(_readback, 0, 0, frameWidth, frameHeight);
        bgfx::blit(2, destination, source);

        _readPixels.resize(static_cast<size_t>(frameWidth) * static_cast<size_t>(frameHeight) * 4);
        bgfx::TextureRegion readRegion;
        readRegion.init(_readback, 0, 0, frameWidth, frameHeight);
        const uint32_t ready = bgfx::read(readRegion, _readPixels.data());
        uint32_t presented = bgfx::frame();
        for (int32_t extra = 0; extra < 2 && presented < ready; extra++)
        {
            presented = bgfx::frame();
        }
        if (presented < ready)
        {
            return false;
        }

        // glGetTexImage stores the bottom row first. The first row handed back is the top of the window.
        const size_t stride = static_cast<size_t>(width) * 4;
        for (int32_t y = 0; y < height; y++)
        {
            const uint8_t* src = _readPixels.data() + (static_cast<size_t>(height - 1 - y) * stride);
            std::memcpy(rgba + static_cast<size_t>(y) * stride, src, stride);
        }
        static bool loggedCapture = false;
        if (!loggedCapture)
        {
            loggedCapture = true;
            std::fprintf(stderr, "View3D: captured %dx%d\n", width, height);
        }
        return true;
    }

    bool renderPark(void* sdlWindow, int32_t width, int32_t height, uint8_t* rgba)
    {
        auto* window = static_cast<SDL_Window*>(sdlWindow);
        if (rgba == nullptr || window == nullptr || width <= 0 || height <= 0 || width > UINT16_MAX || height > UINT16_MAX)
        {
            return false;
        }
        if (_failed)
        {
            return false;
        }

        SDL_GLContext current = SDL_GL_GetCurrentContext();
        if (current == nullptr || SDL_GL_GetCurrentWindow() != window)
        {
            logFailure("3D view needs the drawing engine OpenGL context");
            _failed = true;
            return false;
        }

#if !defined(_WIN32)
        // Linux bgfx can adopt an EGL context only. A GLX context is a different client and cannot be shared.
        using CurrentFn = void* (*)();
        static CurrentFn eglCurrent = nullptr;
        static bool lookedUp = false;
        if (!lookedUp)
        {
            lookedUp = true;
            void* library = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_NOLOAD);
            if (library == nullptr)
            {
                library = dlopen("libEGL.so.1", RTLD_LAZY);
            }
            if (library != nullptr)
            {
                eglCurrent = reinterpret_cast<CurrentFn>(dlsym(library, "eglGetCurrentContext"));
            }
        }
        if (eglCurrent == nullptr || eglCurrent() == nullptr)
        {
            logFailure("OpenGL context is not EGL, so the 3D view cannot use it");
            _failed = true;
            return false;
        }
#endif

        uint16_t backWidth = 0;
        uint16_t backHeight = 0;
        if (!windowPixels(window, backWidth, backHeight))
        {
            return false;
        }

        const auto frameWidth = static_cast<uint16_t>(width);
        const auto frameHeight = static_cast<uint16_t>(height);
        if (!ensureSharedDevice(window, current, backWidth, backHeight) || !ensureFrameTargets(frameWidth, frameHeight))
        {
            SDL_GL_MakeCurrent(window, current);
            return false;
        }

        // Every view that is submitted has to target the offscreen buffer. The window back buffer
        // is the drawing engine's, and presenting it replaces the frame with black.
        bgfx::setViewFrameBuffer(0, _frame);
        bgfx::setViewRect(0, 0, 0, frameWidth, frameHeight);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, kBlankClear, 1.0f, 0);
        bgfx::touch(0);
        Park::draw(frameWidth, frameHeight);

        bgfx::setViewFrameBuffer(2, _frame);
        bgfx::setViewRect(2, 0, 0, frameWidth, frameHeight);
        bgfx::setViewClear(2, BGFX_CLEAR_NONE, 0, 1.0f, 0);
        bgfx::TextureRegion source;
        source.init(bgfx::getTexture(_frame), 0, 0, frameWidth, frameHeight);
        bgfx::TextureRegion destination;
        destination.init(_readback, 0, 0, frameWidth, frameHeight);
        bgfx::blit(2, destination, source);

        _readPixels.resize(static_cast<size_t>(frameWidth) * static_cast<size_t>(frameHeight) * 4);
        bgfx::TextureRegion readRegion;
        readRegion.init(_readback, 0, 0, frameWidth, frameHeight);
        const uint32_t ready = bgfx::read(readRegion, _readPixels.data());
        uint32_t presented = bgfx::frame();
        for (int32_t extra = 0; extra < 2 && presented < ready; extra++)
        {
            presented = bgfx::frame();
        }

        // bgfx rebinds this same context and leaves its own framebuffer, program, and masks current.
        SDL_GL_MakeCurrent(window, current);
        if (presented < ready)
        {
            return false;
        }

        const size_t stride = static_cast<size_t>(width) * 4;
        for (int32_t y = 0; y < height; y++)
        {
            const uint8_t* src = _readPixels.data() + (static_cast<size_t>(height - 1 - y) * stride);
            std::memcpy(rgba + static_cast<size_t>(y) * stride, src, stride);
        }
        static bool logged = false;
        if (!logged)
        {
            logged = true;
            std::fprintf(stderr, "View3D: rendered park %dx%d with the drawing engine context\n", width, height);
        }
        return true;
    }
} // namespace OpenRCT2::View3D
