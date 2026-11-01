/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "MeshDraw.hpp"

#include "GpuBudget.hpp"
#include "SceneryScale.hpp"

#include <algorithm>
#include <bgfx/bgfx.h>
#include <cstdint>
#include <cstring>
#include <stb_image.h>
#include <vector>

namespace OpenRCT2::View3D
{
    namespace
    {
        constexpr uint32_t kVaryingHash = 0x5C3E91A4u;
        constexpr uint8_t kShaderVersion = 0x0c;
        constexpr uint16_t kInstanceStride = 16;
        constexpr const char kSamplerName[] = "s_albedo";
        constexpr const char kParamsName[] = "u_params";
        constexpr const char kUvRectName[] = "u_uvRect";

        constexpr const char kVertexShader[] = "uniform mat4 u_modelViewProj;\n"
                                               "uniform vec4 u_uvRect;\n"
                                               "uniform vec4 u_params;\n"
                                               "in vec3 a_position;\n"
                                               "in vec3 a_normal;\n"
                                               "in vec2 a_texcoord0;\n"
                                               "in vec4 i_data0;\n"
                                               "out vec3 v_normal;\n"
                                               "out vec2 v_uv;\n"
                                               "void main()\n"
                                               "{\n"
                                               "    float c = cos(i_data0.w);\n"
                                               "    float s = sin(i_data0.w);\n"
                                               "    vec3 p = a_position;\n"
                                               "    vec3 rotated = vec3(c * p.x - s * p.y, s * p.x + c * p.y, p.z);\n"
                                               "    vec3 n = a_normal;\n"
                                               "    v_normal = vec3(c * n.x - s * n.y, s * n.x + c * n.y, n.z);\n"
                                               "    vec2 uv = u_params.w > 0.5 ? clamp(a_texcoord0, vec2(0.0), vec2(1.0)) : "
                                               "fract(a_texcoord0);\n"
                                               "    v_uv = uv * u_uvRect.xy + u_uvRect.zw;\n"
                                               "    vec3 world = rotated + i_data0.xyz;\n"
                                               "    gl_Position = u_modelViewProj * vec4(world, 1.0);\n"
                                               "}\n";

        constexpr const char kFragmentShader[] = "uniform sampler2D s_albedo;\n"
                                                 "uniform vec4 u_params;\n"
                                                 "in vec3 v_normal;\n"
                                                 "in vec2 v_uv;\n"
                                                 "layout(location = 0) out vec4 bgfx_FragColor;\n"
                                                 "void main()\n"
                                                 "{\n"
                                                 "    vec4 colour = texture(s_albedo, v_uv);\n"
                                                 "    if (colour.a < u_params.x)\n"
                                                 "    {\n"
                                                 "        discard;\n"
                                                 "    }\n"
                                                 "    vec3 normal = normalize(v_normal);\n"
                                                 "    float lit = abs(dot(normal, normalize(vec3(-0.35, -0.45, 0.82))));\n"
                                                 "    float shade = mix(1.0, 0.55 + 0.45 * lit, u_params.y);\n"
                                                 "    colour.rgb *= shade;\n"
                                                 "    float alpha = u_params.z > 0.5 ? colour.a : 1.0;\n"
                                                 "    bgfx_FragColor = vec4(colour.rgb, alpha);\n"
                                                 "}\n";

        struct DrawPrimitive
        {
            bgfx::DynamicVertexBufferHandle vertices = BGFX_INVALID_HANDLE;
            bgfx::DynamicIndexBufferHandle indices = BGFX_INVALID_HANDLE;
            uint32_t vertexStart = 0;
            uint32_t vertexCount = 0;
            uint32_t indexStart = 0;
            uint32_t indexCount = 0;
            BufferRange vertexRange;
            BufferRange indexRange;
            bool inArena = false;
            bool inAtlas = false;
            AtlasSlot atlasSlot;
            bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
            bool ownsTexture = false;
            float uvScaleX = 1.0f;
            float uvScaleY = 1.0f;
            float uvOffX = 0.0f;
            float uvOffY = 0.0f;
            float alphaCutoff = 0.0f;
            float light = 1.0f;
            bool blend = false;
            bool doubleSided = false;
            bool clampUv = true;
        };

        struct DrawModel
        {
            std::vector<DrawPrimitive> primitives;
        };

        bgfx::ProgramHandle _program = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle _sampler = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle _params = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle _uvRect = BGFX_INVALID_HANDLE;
        bgfx::VertexLayout _layout;
        GeometryArena _arena(kSlabVertices, kSlabIndices, kMaxGeometrySlabs);
        AtlasPacker _atlas(kAtlasSize, kMaxAtlasPages, kAtlasGutter);
        struct SlabGpu
        {
            bgfx::DynamicVertexBufferHandle vertices = BGFX_INVALID_HANDLE;
            bgfx::DynamicIndexBufferHandle indices = BGFX_INVALID_HANDLE;
        };
        std::vector<SlabGpu> _slabs;
        std::vector<bgfx::TextureHandle> _atlasPages;
        bgfx::TextureHandle _white = BGFX_INVALID_HANDLE;
        std::vector<DrawModel> _models;
        std::vector<DrawPrimitive> _sprites;
        uint32_t _ownedTextures = 0;
        bool _ready = false;

        uint32_t estimatedHandles()
        {
            uint32_t count = static_cast<uint32_t>(_slabs.size()) * 2u + static_cast<uint32_t>(_atlasPages.size());
            count += _ownedTextures;
            if (bgfx::isValid(_white))
            {
                count++;
            }
            // Program, three uniforms, and a little slack for the terrain pass.
            count += kHandleHeadroom;
            return count;
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

        void appendName(std::vector<uint8_t>& out, const char* name)
        {
            const auto size = static_cast<uint8_t>(std::strlen(name));
            appendU8(out, size);
            out.insert(out.end(), name, name + size);
        }

        void appendSampler(std::vector<uint8_t>& out, const char* name)
        {
            appendName(out, name);
            appendU8(out, static_cast<uint8_t>(bgfx::UniformType::Sampler));
            appendU8(out, 1);
            appendU16(out, 0);
            appendU16(out, 1);
            appendU8(out, 0);
            appendU8(out, 2);
            appendU16(out, 0);
        }

        void appendVec4(std::vector<uint8_t>& out, const char* name)
        {
            appendName(out, name);
            appendU8(out, static_cast<uint8_t>(bgfx::UniformType::Vec4));
            appendU8(out, 1);
            appendU16(out, 0);
            appendU16(out, 1);
            appendU16(out, 0);
            appendU16(out, 0);
        }

        bgfx::ShaderHandle createShader(char stage, uint32_t hashIn, uint32_t hashOut, bool fragment, const char* source)
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
            if (fragment)
            {
                appendU16(out, 2);
                appendSampler(out, kSamplerName);
                appendVec4(out, kParamsName);
            }
            else
            {
                appendU16(out, 2);
                appendVec4(out, kUvRectName);
                appendVec4(out, kParamsName);
            }
            const auto sourceSize = static_cast<uint32_t>(std::strlen(source) + 1);
            appendU32(out, sourceSize);
            out.insert(out.end(), source, source + sourceSize);
            return bgfx::createShader(bgfx::copy(out.data(), static_cast<uint32_t>(out.size())));
        }

        void destroyPrimitive(DrawPrimitive& primitive)
        {
            if (primitive.inArena)
            {
                _arena.free(primitive.vertexRange, primitive.indexRange);
            }
            if (primitive.inAtlas)
            {
                _atlas.free(primitive.atlasSlot);
            }
            if (primitive.ownsTexture && bgfx::isValid(primitive.texture) && primitive.texture.idx != _white.idx)
            {
                bgfx::destroy(primitive.texture);
                if (_ownedTextures > 0)
                {
                    _ownedTextures--;
                }
            }
            primitive = {};
        }

        void ensureSlab(uint32_t slab)
        {
            while (_slabs.size() <= slab)
            {
                SlabGpu gpu;
                gpu.vertices = bgfx::createDynamicVertexBuffer(kSlabVertices, _layout);
                gpu.indices = bgfx::createDynamicIndexBuffer(kSlabIndices);
                _slabs.push_back(gpu);
            }
        }

        void blitAtlas(bgfx::TextureHandle texture, const AtlasSlot& slot, const uint8_t* rgba, int32_t width, int32_t height)
        {
            const int32_t gutter = static_cast<int32_t>(kAtlasGutter);
            const int32_t paddedW = width + gutter * 2;
            const int32_t paddedH = height + gutter * 2;
            std::vector<uint8_t> padded(static_cast<size_t>(paddedW) * static_cast<size_t>(paddedH) * 4u);
            for (int32_t y = 0; y < paddedH; y++)
            {
                const int32_t srcY = std::clamp(y - gutter, 0, height - 1);
                for (int32_t x = 0; x < paddedW; x++)
                {
                    const int32_t srcX = std::clamp(x - gutter, 0, width - 1);
                    const uint8_t* src = rgba
                        + (static_cast<size_t>(srcY) * static_cast<size_t>(width) + static_cast<size_t>(srcX)) * 4u;
                    uint8_t* dst = padded.data()
                        + (static_cast<size_t>(y) * static_cast<size_t>(paddedW) + static_cast<size_t>(x)) * 4u;
                    std::memcpy(dst, src, 4);
                }
            }
            bgfx::updateTexture2D(
                texture, 0, 0, static_cast<uint16_t>(slot.x - static_cast<uint32_t>(gutter)),
                static_cast<uint16_t>(slot.y - static_cast<uint32_t>(gutter)), static_cast<uint16_t>(paddedW),
                static_cast<uint16_t>(paddedH), bgfx::copy(padded.data(), static_cast<uint32_t>(padded.size())));
        }

        bgfx::TextureHandle atlasTexture(uint32_t page)
        {
            while (_atlasPages.size() <= page)
            {
                const uint64_t flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
                _atlasPages.push_back(
                    bgfx::createTexture2D(
                        static_cast<uint16_t>(kAtlasSize), static_cast<uint16_t>(kAtlasSize), false, 1,
                        bgfx::TextureFormat::RGBA8, flags, nullptr));
            }
            return _atlasPages[page];
        }

        bgfx::TextureHandle uploadRgba(const uint8_t* rgba, uint16_t width, uint16_t height, bool nearest, bool clamp)
        {
            if (rgba == nullptr || width == 0 || height == 0)
            {
                return BGFX_INVALID_HANDLE;
            }
            const bgfx::Caps* caps = bgfx::getCaps();
            if (caps != nullptr && (width > caps->limits.maxTextureSize || height > caps->limits.maxTextureSize))
            {
                return BGFX_INVALID_HANDLE;
            }
            uint64_t flags = BGFX_SAMPLER_UVW_CLAMP;
            if (!clamp)
            {
                flags = 0;
            }
            if (nearest)
            {
                flags |= BGFX_SAMPLER_POINT | BGFX_SAMPLER_MIP_POINT;
            }
            const uint32_t bytes = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 4u;
            return bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::RGBA8, flags, bgfx::copy(rgba, bytes));
        }

        bool uploadPrimitive(
            const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices, bgfx::TextureHandle texture,
            float alphaCutoff, float light, bool blend, bool doubleSided, DrawPrimitive& out)
        {
            if (vertices.empty() || indices.empty() || vertices.size() > 65535 || !bgfx::isValid(texture))
            {
                return false;
            }
            BufferRange vertexRange;
            BufferRange indexRange;
            if (!_arena.alloc(
                    static_cast<uint32_t>(vertices.size()), static_cast<uint32_t>(indices.size()), vertexRange, indexRange))
            {
                return false;
            }
            ensureSlab(vertexRange.slab);
            std::vector<uint16_t> index16(indices.size());
            for (size_t i = 0; i < indices.size(); i++)
            {
                // Mesh-local. bgfx::setVertexBuffer's start vertex is the base (renderer_gl bindAttributes).
                index16[i] = static_cast<uint16_t>(indices[i]);
            }
            const SlabGpu& slab = _slabs[vertexRange.slab];
            if (!bgfx::isValid(slab.vertices) || !bgfx::isValid(slab.indices))
            {
                _arena.free(vertexRange, indexRange);
                return false;
            }
            bgfx::update(
                slab.vertices, vertexRange.offset,
                bgfx::copy(vertices.data(), static_cast<uint32_t>(vertices.size() * sizeof(MeshVertex))));
            bgfx::update(
                slab.indices, indexRange.offset,
                bgfx::copy(index16.data(), static_cast<uint32_t>(index16.size() * sizeof(uint16_t))));
            out.vertices = slab.vertices;
            out.indices = slab.indices;
            out.vertexStart = vertexRange.offset;
            out.vertexCount = vertexRange.count;
            out.indexStart = indexRange.offset;
            out.indexCount = indexRange.count;
            out.vertexRange = vertexRange;
            out.indexRange = indexRange;
            out.inArena = true;
            out.texture = texture;
            out.alphaCutoff = alphaCutoff;
            out.light = light;
            out.blend = blend;
            out.doubleSided = doubleSided;
            return bgfx::isValid(out.vertices) && bgfx::isValid(out.indices);
        }

        void submitPrimitive(const DrawPrimitive& primitive, const MeshInstance* instances, uint32_t count)
        {
            if (count == 0 || !bgfx::isValid(primitive.vertices) || !bgfx::isValid(_program))
            {
                return;
            }
            uint32_t drawn = 0;
            while (drawn < count)
            {
                const uint32_t avail = bgfx::getAvailInstanceDataBuffer(count - drawn, kInstanceStride);
                if (avail == 0)
                {
                    break;
                }
                bgfx::InstanceDataBuffer buffer;
                bgfx::allocInstanceDataBuffer(&buffer, avail, kInstanceStride);
                std::memcpy(buffer.data, instances + drawn, static_cast<size_t>(avail) * kInstanceStride);

                uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS;
                if (!primitive.blend)
                {
                    state |= BGFX_STATE_WRITE_Z;
                }
                else
                {
                    state |= BGFX_STATE_BLEND_ALPHA;
                }
                // Keep the counter-clockwise side. That is the side whose normal
                // points at the isometric camera. bgfx's projection and ParkView's
                // mirrored camera X both reverse winding, so they cancel.
                if (!primitive.doubleSided)
                {
                    state |= BGFX_STATE_CULL_CW;
                }
                const float params[4] = { primitive.alphaCutoff, primitive.light, primitive.blend ? 1.0f : 0.0f,
                                          primitive.clampUv ? 1.0f : 0.0f };
                const float uvRect[4] = { primitive.uvScaleX, primitive.uvScaleY, primitive.uvOffX, primitive.uvOffY };
                bgfx::setState(state);
                bgfx::setTexture(0, _sampler, primitive.texture);
                bgfx::setUniform(_params, params);
                bgfx::setUniform(_uvRect, uvRect);
                bgfx::setVertexBuffer(0, primitive.vertices, primitive.vertexStart, primitive.vertexCount);
                bgfx::setIndexBuffer(primitive.indices, primitive.indexStart, primitive.indexCount);
                bgfx::setInstanceDataBuffer(&buffer);
                bgfx::submit(0, _program);
                drawn += avail;
            }
        }
    } // namespace

    bool meshDrawInit()
    {
        if (_ready)
        {
            return bgfx::isValid(_program);
        }
        _ready = true;
        _sampler = bgfx::createUniform(kSamplerName, bgfx::UniformType::Sampler);
        _params = bgfx::createUniform(kParamsName, bgfx::UniformType::Vec4);
        _uvRect = bgfx::createUniform(kUvRectName, bgfx::UniformType::Vec4);
        const bgfx::ShaderHandle vertex = createShader('V', 0, kVaryingHash, false, kVertexShader);
        const bgfx::ShaderHandle fragment = createShader('F', kVaryingHash, 0, true, kFragmentShader);
        _program = bgfx::createProgram(vertex, fragment, true);
        _layout.begin(bgfx::RendererType::OpenGL)
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .end();
        const uint8_t white[4] = { 255, 255, 255, 255 };
        _white = uploadRgba(white, 1, 1, true, true);
        return bgfx::isValid(_program) && bgfx::isValid(_sampler) && bgfx::isValid(_params) && bgfx::isValid(_white);
    }

    void meshDrawShutdown()
    {
        for (DrawModel& model : _models)
        {
            for (DrawPrimitive& primitive : model.primitives)
            {
                destroyPrimitive(primitive);
            }
        }
        _models.clear();
        for (DrawPrimitive& sprite : _sprites)
        {
            destroyPrimitive(sprite);
        }
        _sprites.clear();
        if (bgfx::isValid(_white))
        {
            bgfx::destroy(_white);
        }
        _white = BGFX_INVALID_HANDLE;
        if (bgfx::isValid(_program))
        {
            bgfx::destroy(_program);
        }
        _program = BGFX_INVALID_HANDLE;
        if (bgfx::isValid(_sampler))
        {
            bgfx::destroy(_sampler);
        }
        _sampler = BGFX_INVALID_HANDLE;
        if (bgfx::isValid(_params))
        {
            bgfx::destroy(_params);
        }
        _params = BGFX_INVALID_HANDLE;
        if (bgfx::isValid(_uvRect))
        {
            bgfx::destroy(_uvRect);
        }
        _uvRect = BGFX_INVALID_HANDLE;
        for (SlabGpu& slab : _slabs)
        {
            if (bgfx::isValid(slab.vertices))
            {
                bgfx::destroy(slab.vertices);
            }
            if (bgfx::isValid(slab.indices))
            {
                bgfx::destroy(slab.indices);
            }
        }
        _slabs.clear();
        for (bgfx::TextureHandle page : _atlasPages)
        {
            if (bgfx::isValid(page))
            {
                bgfx::destroy(page);
            }
        }
        _atlasPages.clear();
        _arena = GeometryArena(kSlabVertices, kSlabIndices, kMaxGeometrySlabs);
        _atlas = AtlasPacker(kAtlasSize, kMaxAtlasPages, kAtlasGutter);
        _ownedTextures = 0;
        _ready = false;
    }

    void meshDrawReleaseModel(int32_t id)
    {
        if (id < 0 || static_cast<size_t>(id) >= _models.size())
        {
            return;
        }
        for (DrawPrimitive& primitive : _models[static_cast<size_t>(id)].primitives)
        {
            destroyPrimitive(primitive);
        }
        _models[static_cast<size_t>(id)].primitives.clear();
    }

    void meshDrawClearSprites()
    {
        for (DrawPrimitive& sprite : _sprites)
        {
            destroyPrimitive(sprite);
        }
        _sprites.clear();
    }

    int32_t meshDrawUploadModel(const CpuMesh& mesh)
    {
        if (!meshDrawInit() || !bgfx::isValid(_program))
        {
            return -1;
        }
        DrawModel model;
        auto abandon = [&]() {
            for (DrawPrimitive& primitive : model.primitives)
            {
                destroyPrimitive(primitive);
            }
            model.primitives.clear();
            return -1;
        };
        for (const MeshPrimitive& primitive : mesh.primitives)
        {
            bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
            bool sharedTexture = false;
            bool packed = false;
            bool clampUv = true;
            float uvScaleX = 1.0f;
            float uvScaleY = 1.0f;
            float uvOffX = 0.0f;
            float uvOffY = 0.0f;
            AtlasSlot slot;
            const bool wantsImage = primitive.image >= 0;
            if (wantsImage)
            {
                if (static_cast<size_t>(primitive.image) >= mesh.images.size())
                {
                    return abandon();
                }
                const MeshImage& image = mesh.images[static_cast<size_t>(primitive.image)];
                const bool rgbaReady = !image.rgba.empty() && image.width > 0 && image.height > 0
                    && image.rgba.size() >= static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4u;
                if (!rgbaReady)
                {
                    return abandon();
                }
                clampUv = image.clamp;
                const uint32_t padded = kAtlasGutter * 2u;
                const bool atlasFit = image.nearest
                    && image.width + static_cast<int32_t>(padded) <= static_cast<int32_t>(kAtlasSize)
                    && image.height + static_cast<int32_t>(padded) <= static_cast<int32_t>(kAtlasSize);
                if (atlasFit && _atlas.pack(static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height), slot))
                {
                    packed = true;
                    texture = atlasTexture(slot.page);
                    sharedTexture = bgfx::isValid(texture);
                    if (sharedTexture)
                    {
                        blitAtlas(texture, slot, image.rgba.data(), image.width, image.height);
                        uvScaleX = static_cast<float>(image.width) / static_cast<float>(kAtlasSize);
                        uvScaleY = static_cast<float>(image.height) / static_cast<float>(kAtlasSize);
                        uvOffX = static_cast<float>(slot.x) / static_cast<float>(kAtlasSize);
                        uvOffY = static_cast<float>(slot.y) / static_cast<float>(kAtlasSize);
                    }
                    else
                    {
                        _atlas.free(slot);
                        packed = false;
                    }
                }
                if (!sharedTexture)
                {
                    if (estimatedHandles() + 1u >= kHandleCap)
                    {
                        return abandon();
                    }
                    texture = uploadRgba(
                        image.rgba.data(), static_cast<uint16_t>(image.width), static_cast<uint16_t>(image.height),
                        image.nearest, true);
                    if (!bgfx::isValid(texture))
                    {
                        return abandon();
                    }
                }
            }
            else
            {
                texture = _white;
            }
            const float cutoff = primitive.alpha == MeshAlpha::mask ? primitive.alphaCutoff : 0.0f;
            DrawPrimitive gpu;
            if (!uploadPrimitive(
                    primitive.vertices, primitive.indices, texture, cutoff, 1.0f, primitive.alpha == MeshAlpha::blend,
                    primitive.doubleSided, gpu))
            {
                if (packed)
                {
                    _atlas.free(slot);
                }
                else if (bgfx::isValid(texture) && texture.idx != _white.idx)
                {
                    bgfx::destroy(texture);
                }
                return abandon();
            }
            gpu.inAtlas = sharedTexture;
            gpu.atlasSlot = slot;
            gpu.ownsTexture = !sharedTexture && texture.idx != _white.idx;
            gpu.clampUv = clampUv;
            if (gpu.ownsTexture)
            {
                _ownedTextures++;
            }
            gpu.uvScaleX = uvScaleX;
            gpu.uvScaleY = uvScaleY;
            gpu.uvOffX = uvOffX;
            gpu.uvOffY = uvOffY;
            model.primitives.push_back(gpu);
        }
        if (model.primitives.empty())
        {
            return -1;
        }
        _models.push_back(std::move(model));
        return static_cast<int32_t>(_models.size() - 1);
    }

    int32_t meshDrawUploadSprite(const SpriteQuad& quad, const uint8_t* rgba, int32_t width, int32_t height)
    {
        if (!meshDrawInit() || rgba == nullptr || width <= 0 || height <= 0 || width > 65535 || height > 65535)
        {
            return -1;
        }
        bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
        bool sharedTexture = false;
        float uvScaleX = 1.0f;
        float uvScaleY = 1.0f;
        float uvOffX = 0.0f;
        float uvOffY = 0.0f;
        AtlasSlot slot;
        const bool fitsAtlas = width <= static_cast<int32_t>(kAtlasSize) && height <= static_cast<int32_t>(kAtlasSize)
            && _atlas.pack(static_cast<uint32_t>(width), static_cast<uint32_t>(height), slot);
        if (fitsAtlas)
        {
            texture = atlasTexture(slot.page);
            sharedTexture = bgfx::isValid(texture);
            if (sharedTexture)
            {
                blitAtlas(texture, slot, rgba, width, height);
                uvScaleX = static_cast<float>(width) / static_cast<float>(kAtlasSize);
                uvScaleY = static_cast<float>(height) / static_cast<float>(kAtlasSize);
                uvOffX = static_cast<float>(slot.x) / static_cast<float>(kAtlasSize);
                uvOffY = static_cast<float>(slot.y) / static_cast<float>(kAtlasSize);
            }
            else
            {
                _atlas.free(slot);
            }
        }
        if (!sharedTexture)
        {
            if (estimatedHandles() + 1u >= kHandleCap)
            {
                return -1;
            }
            texture = uploadRgba(rgba, static_cast<uint16_t>(width), static_cast<uint16_t>(height), true, true);
        }
        if (!bgfx::isValid(texture))
        {
            return -1;
        }
        std::vector<MeshVertex> vertices(4);
        const float uvs[4][2] = {
            { 0.0f, 1.0f },
            { 1.0f, 1.0f },
            { 1.0f, 0.0f },
            { 0.0f, 0.0f },
        };
        for (int i = 0; i < 4; i++)
        {
            vertices[static_cast<size_t>(i)].x = quad.corner[i].x;
            vertices[static_cast<size_t>(i)].y = quad.corner[i].y;
            vertices[static_cast<size_t>(i)].z = quad.corner[i].z;
            vertices[static_cast<size_t>(i)].nx = quad.normal.x;
            vertices[static_cast<size_t>(i)].ny = quad.normal.y;
            vertices[static_cast<size_t>(i)].nz = quad.normal.z;
            vertices[static_cast<size_t>(i)].u = uvs[i][0];
            vertices[static_cast<size_t>(i)].v = uvs[i][1];
        }
        const std::vector<uint32_t> indices = { 0, 1, 2, 0, 2, 3 };
        DrawPrimitive gpu;
        if (!uploadPrimitive(vertices, indices, texture, 0.5f, 0.0f, false, false, gpu))
        {
            if (sharedTexture)
            {
                _atlas.free(slot);
            }
            else if (bgfx::isValid(texture))
            {
                bgfx::destroy(texture);
            }
            return -1;
        }
        gpu.inAtlas = sharedTexture;
        gpu.atlasSlot = slot;
        gpu.clampUv = true;
        gpu.ownsTexture = !sharedTexture;
        if (gpu.ownsTexture)
        {
            _ownedTextures++;
        }
        gpu.uvScaleX = uvScaleX;
        gpu.uvScaleY = uvScaleY;
        gpu.uvOffX = uvOffX;
        gpu.uvOffY = uvOffY;
        _sprites.push_back(gpu);
        return static_cast<int32_t>(_sprites.size() - 1);
    }

    void meshDrawModel(int32_t id, const MeshInstance* instances, uint32_t count)
    {
        if (id < 0 || static_cast<size_t>(id) >= _models.size())
        {
            return;
        }
        for (const DrawPrimitive& primitive : _models[static_cast<size_t>(id)].primitives)
        {
            submitPrimitive(primitive, instances, count);
        }
    }

    void meshDrawSprite(int32_t id, const MeshInstance* instances, uint32_t count)
    {
        if (id < 0 || static_cast<size_t>(id) >= _sprites.size())
        {
            return;
        }
        submitPrimitive(_sprites[static_cast<size_t>(id)], instances, count);
    }
} // namespace OpenRCT2::View3D
