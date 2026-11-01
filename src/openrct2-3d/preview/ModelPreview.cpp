/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

// Offscreen render of one GLB on a ground quad, with a crossed sprite beside it.
// Usage: openrct2-3d-preview <model.glb> <output-directory>

#include "GlbMesh.hpp"
#include "MeshDraw.hpp"
#include "SpriteQuad.hpp"
#include "ViewerScale.hpp"

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    constexpr uint16_t kWidth = 1280;
    constexpr uint16_t kHeight = 720;
    constexpr uint32_t kClear = 0x6FA8DCff;

    bgfx::FrameBufferHandle _frame = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle _readback = BGFX_INVALID_HANDLE;

    bool createTargets()
    {
        const uint64_t colourFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        const uint64_t depthFlags = BGFX_TEXTURE_RT_WRITE_ONLY;
        const bgfx::TextureHandle colour = bgfx::createTexture2D(
            kWidth, kHeight, false, 1, bgfx::TextureFormat::RGBA8, colourFlags);
        const bgfx::TextureFormat::Enum depthFormats[] = {
            bgfx::TextureFormat::D24,
            bgfx::TextureFormat::D32F,
            bgfx::TextureFormat::D16,
            bgfx::TextureFormat::D24S8,
        };
        bgfx::TextureHandle depth = BGFX_INVALID_HANDLE;
        for (const bgfx::TextureFormat::Enum format : depthFormats)
        {
            if (!bgfx::isTextureValid(0, false, 1, format, depthFlags))
            {
                continue;
            }
            depth = bgfx::createTexture2D(kWidth, kHeight, false, 1, format, depthFlags);
            if (bgfx::isValid(depth))
            {
                break;
            }
        }
        if (!bgfx::isValid(colour) || !bgfx::isValid(depth))
        {
            return false;
        }
        const bgfx::TextureHandle attachments[] = { colour, depth };
        _frame = bgfx::createFrameBuffer(2, attachments, true);
        _readback = bgfx::createTexture2D(
            kWidth, kHeight, false, 1, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        return bgfx::isValid(_frame) && bgfx::isValid(_readback);
    }

    void setCamera(float eyeX, float eyeY, float eyeZ, float targetX, float targetY, float targetZ, bool mirrorX)
    {
        float view[16];
        float projection[16];
        bx::mtxLookAt(view, bx::Vec3(eyeX, eyeY, eyeZ), bx::Vec3(targetX, targetY, targetZ), bx::Vec3(0.0f, 0.0f, 1.0f));
        if (mirrorX)
        {
            view[0] = -view[0];
            view[4] = -view[4];
            view[8] = -view[8];
            view[12] = -view[12];
        }
        const float aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
        bx::mtxProj(projection, 50.0f, aspect, 0.2f, 200.0f, bgfx::getCaps()->homogeneousDepth);
        bgfx::setViewTransform(0, view, projection);
    }

    bool savePpm(const std::string& path, const std::vector<uint8_t>& rgba)
    {
        std::ofstream out(path, std::ios::binary);
        if (!out)
        {
            return false;
        }
        out << "P6\n" << kWidth << " " << kHeight << "\n255\n";
        const size_t stride = static_cast<size_t>(kWidth) * 4;
        for (uint16_t y = 0; y < kHeight; y++)
        {
            // bgfx hands the bottom row back first.
            const uint8_t* src = rgba.data() + (static_cast<size_t>(kHeight - 1 - y) * stride);
            for (uint16_t x = 0; x < kWidth; x++)
            {
                out.write(reinterpret_cast<const char*>(src + static_cast<size_t>(x) * 4), 3);
            }
        }
        return static_cast<bool>(out);
    }

    double redVariance(const std::vector<uint8_t>& rgba)
    {
        double sum = 0.0;
        double sumSq = 0.0;
        const size_t count = rgba.size() / 4;
        for (size_t i = 0; i < count; i++)
        {
            const double red = rgba[i * 4];
            sum += red;
            sumSq += red * red;
        }
        const double mean = sum / static_cast<double>(count);
        return sumSq / static_cast<double>(count) - mean * mean;
    }

    bool renderShot(const std::string& path, int32_t model, int32_t ground, const int32_t sprites[4])
    {
        bgfx::setViewFrameBuffer(0, _frame);
        bgfx::setViewRect(0, 0, 0, kWidth, kHeight);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, kClear, 1.0f, 0);
        bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
        bgfx::touch(0);

        const OpenRCT2::View3D::MeshInstance groundInstance{ 0.0f, 0.0f, 0.0f, 0.0f };
        const OpenRCT2::View3D::MeshInstance tree{ 0.0f, 0.0f, 0.0f, 0.0f };
        const OpenRCT2::View3D::MeshInstance cross{ 3.0f, 0.0f, 0.0f, 0.0f };
        OpenRCT2::View3D::meshDrawModel(ground, &groundInstance, 1);
        OpenRCT2::View3D::meshDrawModel(model, &tree, 1);
        for (int face = 0; face < 4; face++)
        {
            OpenRCT2::View3D::meshDrawSprite(sprites[face], &cross, 1);
        }

        bgfx::TextureRegion source;
        source.init(bgfx::getTexture(_frame), 0, 0, kWidth, kHeight);
        bgfx::TextureRegion destination;
        destination.init(_readback, 0, 0, kWidth, kHeight);
        bgfx::blit(2, destination, source);

        std::vector<uint8_t> pixels(static_cast<size_t>(kWidth) * kHeight * 4);
        bgfx::TextureRegion readRegion;
        readRegion.init(_readback, 0, 0, kWidth, kHeight);
        const uint32_t ready = bgfx::read(readRegion, pixels.data());
        uint32_t presented = bgfx::frame();
        for (int extra = 0; extra < 3 && presented < ready; extra++)
        {
            presented = bgfx::frame();
        }
        if (presented < ready)
        {
            std::fprintf(stderr, "preview: readback did not finish for %s\n", path.c_str());
            return false;
        }
        if (redVariance(pixels) < 20.0)
        {
            std::fprintf(stderr, "preview: %s looks blank\n", path.c_str());
            return false;
        }
        if (!savePpm(path, pixels))
        {
            std::fprintf(stderr, "preview: could not write %s\n", path.c_str());
            return false;
        }
        std::fprintf(stderr, "preview: wrote %s\n", path.c_str());
        return true;
    }

    void fillSprite(std::vector<uint8_t>& rgba, uint8_t r, uint8_t g, uint8_t b)
    {
        constexpr int kSize = 64;
        rgba.assign(static_cast<size_t>(kSize * kSize * 4), 0);
        for (int y = 0; y < kSize; y++)
        {
            for (int x = 0; x < kSize; x++)
            {
                uint8_t* pixel = rgba.data() + (static_cast<size_t>(y * kSize + x) * 4);
                const bool border = x < 3 || y < 3 || x >= kSize - 3 || y >= kSize - 3;
                const bool mark = (x > 8 && x < 24 && y > 8 && y < 24);
                pixel[0] = border ? 255 : (mark ? 255 : r);
                pixel[1] = border ? 255 : (mark ? 255 : g);
                pixel[2] = border ? 255 : (mark ? 255 : b);
                pixel[3] = 255;
            }
        }
    }

    int32_t makeGround()
    {
        OpenRCT2::View3D::CpuMesh mesh;
        OpenRCT2::View3D::MeshImage image;
        image.width = 2;
        image.height = 2;
        image.nearest = true;
        image.clamp = false;
        const uint8_t checker[] = {
            90, 140, 70, 255, 70, 110, 55, 255, 70, 110, 55, 255, 90, 140, 70, 255,
        };
        image.rgba.assign(checker, checker + sizeof(checker));
        mesh.images.push_back(std::move(image));

        OpenRCT2::View3D::MeshPrimitive primitive;
        primitive.image = 0;
        primitive.doubleSided = true;
        const float corners[4][5] = {
            { -2.0f, -3.0f, 0.0f, 0.0f, 0.0f },
            { 6.0f, -3.0f, 0.0f, 4.0f, 0.0f },
            { 6.0f, 3.0f, 0.0f, 4.0f, 3.0f },
            { -2.0f, 3.0f, 0.0f, 0.0f, 3.0f },
        };
        for (const float* corner : corners)
        {
            OpenRCT2::View3D::MeshVertex vertex{};
            vertex.x = corner[0];
            vertex.y = corner[1];
            vertex.z = corner[2];
            vertex.nz = 1.0f;
            vertex.u = corner[3];
            vertex.v = corner[4];
            primitive.vertices.push_back(vertex);
        }
        primitive.indices = { 0, 1, 2, 0, 2, 3 };
        mesh.primitives.push_back(std::move(primitive));
        return OpenRCT2::View3D::meshDrawUploadModel(mesh);
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: openrct2-3d-preview <model.glb> <output-directory>\n");
        return 2;
    }
    const std::string glbPath = argv[1];
    const std::string outDir = argv[2];

    std::string error;
    const auto mesh = OpenRCT2::View3D::loadGlbFile(glbPath, {}, error);
    if (!mesh.has_value())
    {
        std::fprintf(stderr, "preview: load failed: %s\n", error.c_str());
        return 1;
    }

    bgfx::Init init;
    init.type = bgfx::RendererType::OpenGL;
    init.fallback = false;
    init.reset = BGFX_RESET_NONE;
    init.swapChain.width = 0;
    init.swapChain.height = 0;
    bgfx::renderFrame();
    if (!bgfx::init(init))
    {
        std::fprintf(stderr, "preview: bgfx init failed\n");
        return 1;
    }
    std::fprintf(stderr, "preview: renderer %s\n", bgfx::getRendererName(bgfx::getRendererType()));
    if (!createTargets())
    {
        std::fprintf(stderr, "preview: framebuffer failed\n");
        bgfx::shutdown();
        return 1;
    }

    const int32_t model = OpenRCT2::View3D::meshDrawUploadModel(*mesh);
    const int32_t ground = makeGround();
    int32_t sprites[4] = { -1, -1, -1, -1 };
    const uint8_t colours[4][3] = {
        { 210, 40, 40 },
        { 40, 180, 50 },
        { 40, 60, 210 },
        { 230, 200, 40 },
    };
    for (int face = 0; face < 4; face++)
    {
        std::vector<uint8_t> rgba;
        fillSprite(rgba, colours[face][0], colours[face][1], colours[face][2]);
        const OpenRCT2::View3D::SpriteQuad quad = OpenRCT2::View3D::makeDirectionSpriteQuad(face, 64, 64, -32, -64);
        sprites[face] = OpenRCT2::View3D::meshDrawUploadSprite(quad, rgba.data(), 64, 64);
    }
    if (model < 0 || ground < 0 || sprites[0] < 0)
    {
        std::fprintf(stderr, "preview: upload failed\n");
        OpenRCT2::View3D::meshDrawShutdown();
        bgfx::shutdown();
        return 1;
    }

    const float targetX = 1.5f;
    const float targetY = 0.0f;
    const float targetZ = 1.6f;
    const float distance = 14.0f;
    const float horizontal = distance * std::cos(OpenRCT2::View3D::kIsometricPitchRadians);
    const float vertical = distance * std::sin(OpenRCT2::View3D::kIsometricPitchRadians);
    constexpr float kDiag = 0.70710678f;

    bool ok = true;
    setCamera(targetX + kDiag * horizontal, targetY + kDiag * horizontal, targetZ + vertical, targetX, targetY, targetZ, true);
    ok = renderShot(outDir + "/scenery-iso-rot0.ppm", model, ground, sprites) && ok;

    setCamera(targetX - kDiag * horizontal, targetY - kDiag * horizontal, targetZ + vertical, targetX, targetY, targetZ, true);
    ok = renderShot(outDir + "/scenery-iso-rot2.ppm", model, ground, sprites) && ok;

    setCamera(targetX, targetY - 0.4f, 16.0f, targetX, targetY, 0.0f, false);
    ok = renderShot(outDir + "/scenery-top.ppm", model, ground, sprites) && ok;

    OpenRCT2::View3D::meshDrawShutdown();
    if (bgfx::isValid(_frame))
    {
        bgfx::destroy(_frame);
    }
    if (bgfx::isValid(_readback))
    {
        bgfx::destroy(_readback);
    }
    bgfx::shutdown();
    return ok ? 0 : 1;
}
