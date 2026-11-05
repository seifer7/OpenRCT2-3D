/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "MeshDraw.hpp"

#include "FilamentRenderer.hpp"
#include "GpuBudget.hpp"
#include "SceneryScale.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace OpenRCT2::View3D
{
    namespace
    {
        struct DrawPrimitive
        {
            Gpu::Geometry geometry;
            filament::MaterialInstance* material = nullptr;
            BufferRange vertexRange;
            BufferRange indexRange;
            bool inArena = false;
            bool inAtlas = false;
            AtlasSlot atlasSlot;
            Gpu::Texture texture;
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

        GeometryArena _arena(kSlabVertices, kSlabIndices, kMaxGeometrySlabs);
        AtlasPacker _atlas(kAtlasSize, kMaxAtlasPages, kAtlasGutter);
        struct SlabGpu
        {
            filament::VertexBuffer* vertices = nullptr;
            filament::IndexBuffer* indices = nullptr;
        };
        std::vector<SlabGpu> _slabs;
        std::vector<Gpu::Texture> _atlasPages;
        Gpu::Texture _white;
        std::vector<DrawModel> _models;
        std::vector<int32_t> _freeModelIds;
        std::vector<DrawPrimitive> _sprites;
        SpriteIdPool _spriteIds;
        struct DistantBatch
        {
            Gpu::Geometry geometry;
            filament::MaterialInstance* material = nullptr;
            uint32_t capacity = 0;
        };
        std::vector<DistantBatch> _distantBatches;
        size_t _distantUsed = 0;
        uint32_t _ownedTextures = 0;
        bool _ready = false;

        uint32_t estimatedHandles()
        {
            uint32_t count = static_cast<uint32_t>(_slabs.size()) * 2u + static_cast<uint32_t>(_atlasPages.size());
            count += _ownedTextures;
            if (_white.handle != nullptr)
            {
                count++;
            }
            // Reserve room for materials, frame targets, and terrain resources.
            count += kHandleHeadroom;
            return count;
        }

        void destroyPrimitive(DrawPrimitive& primitive)
        {
            Gpu::destroy(primitive.material);
            if (primitive.inArena)
            {
                _arena.free(primitive.vertexRange, primitive.indexRange);
            }
            if (primitive.inAtlas)
            {
                _atlas.free(primitive.atlasSlot);
            }
            if (primitive.ownsTexture && primitive.texture.handle != nullptr && primitive.texture.handle != _white.handle)
            {
                Gpu::destroy(primitive.texture);
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
                gpu.vertices = Gpu::createVertices(kSlabVertices);
                gpu.indices = Gpu::createIndices(kSlabIndices);
                _slabs.push_back(gpu);
            }
        }

        void blitAtlas(Gpu::Texture texture, const AtlasSlot& slot, const uint8_t* rgba, int32_t width, int32_t height)
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
            Gpu::updateTexture(texture, padded.data(), static_cast<uint32_t>(paddedW), static_cast<uint32_t>(paddedH),
                slot.x - static_cast<uint32_t>(gutter), slot.y - static_cast<uint32_t>(gutter));
        }

        Gpu::Texture atlasTexture(uint32_t page)
        {
            while (_atlasPages.size() <= page)
            {
                _atlasPages.push_back(Gpu::createTexture(kAtlasSize, kAtlasSize));
            }
            return _atlasPages[page];
        }

        Gpu::Texture uploadRgba(const uint8_t* rgba, uint16_t width, uint16_t height, bool nearest, bool clamp)
        {
            if (rgba == nullptr || width == 0 || height == 0)
            {
                return {};
            }
            Gpu::Texture texture = Gpu::createTexture(width, height, 1, nearest, clamp);
            Gpu::updateTexture(texture, rgba, width, height);
            return texture;
        }

        bool uploadPrimitive(
            const std::vector<MeshVertex>& vertices, const std::vector<uint32_t>& indices, Gpu::Texture texture,
            float alphaCutoff, float light, bool blend, bool doubleSided, DrawPrimitive& out)
        {
            if (vertices.empty() || indices.empty() || vertices.size() > 65535 || texture.handle == nullptr)
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
            std::vector<uint32_t> slabIndices(indices.size());
            for (size_t i = 0; i < indices.size(); i++)
            {
                if (indices[i] >= vertices.size())
                {
                    _arena.free(vertexRange, indexRange);
                    return false;
                }
                slabIndices[i] = slabVertexIndex(vertexRange.offset, indices[i]);
            }
            const SlabGpu& slab = _slabs[vertexRange.slab];
            if (slab.vertices == nullptr || slab.indices == nullptr)
            {
                _arena.free(vertexRange, indexRange);
                return false;
            }
            std::vector<Gpu::Vertex> converted(vertices.size());
            for (size_t i = 0; i < vertices.size(); ++i)
            {
                const auto& v = vertices[i];
                converted[i] = { v.x, v.y, v.z, v.nx, v.ny, v.nz, v.u, v.v };
            }
            Gpu::updateVertices(slab.vertices, converted.data(), vertexRange.count, vertexRange.offset);
            Gpu::updateIndices(slab.indices, slabIndices.data(), indexRange.count, indexRange.offset);
            out.geometry = { slab.vertices, slab.indices, indexRange.offset, indexRange.count,
                Gpu::bounds(converted.data(), vertexRange.count) };
            const auto kind = light == 0.0f ? Gpu::MaterialKind::sprite : blend ? Gpu::MaterialKind::blended
                : alphaCutoff > 0.0f ? Gpu::MaterialKind::masked : Gpu::MaterialKind::opaque;
            out.material = Gpu::createMaterial(kind);
            if (out.material == nullptr)
            {
                _arena.free(vertexRange, indexRange);
                out = {};
                return false;
            }
            out.vertexRange = vertexRange;
            out.indexRange = indexRange;
            out.inArena = true;
            out.texture = texture;
            out.alphaCutoff = alphaCutoff;
            out.light = light;
            out.blend = blend;
            out.doubleSided = doubleSided;
            return true;
        }

        void submitPrimitive(const DrawPrimitive& primitive, const MeshInstance* instances, uint32_t count)
        {
            if (count == 0 || instances == nullptr || primitive.geometry.vertices == nullptr || primitive.material == nullptr)
            {
                return;
            }
            std::vector<filament::math::mat4f> transforms(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto& instance = instances[i];
                transforms[i] = Gpu::instanceTransform(instance.x, instance.y, instance.z, instance.yaw);
            }
            const float uvRect[4] = { primitive.uvScaleX, primitive.uvScaleY, primitive.uvOffX, primitive.uvOffY };
            Gpu::configureMesh(primitive.material, primitive.texture, uvRect, primitive.clampUv,
                primitive.alphaCutoff, primitive.doubleSided);
            Gpu::draw(primitive.geometry, primitive.material, transforms.data(), count, primitive.light != 0.0f);
        }
    } // namespace

    bool meshDrawInit()
    {
        if (_ready)
        {
            return Gpu::ready() && _white.handle != nullptr;
        }
        if (!Gpu::ready())
        {
            return false;
        }
        const uint8_t white[4] = { 255, 255, 255, 255 };
        _white = uploadRgba(white, 1, 1, true, true);
        _ready = _white.handle != nullptr;
        return _ready;
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
        _freeModelIds.clear();
        for (DrawPrimitive& sprite : _sprites)
        {
            destroyPrimitive(sprite);
        }
        _spriteIds.clear();
        _sprites.clear();
        for (auto& batch : _distantBatches)
        {
            Gpu::destroy(batch.material);
            Gpu::destroy(batch.geometry);
        }
        _distantBatches.clear();
        _distantUsed = 0;
        Gpu::destroy(_white);
        for (SlabGpu& slab : _slabs)
        {
            Gpu::destroy(slab.vertices);
            Gpu::destroy(slab.indices);
        }
        _slabs.clear();
        for (auto& page : _atlasPages)
        {
            Gpu::destroy(page);
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
        _freeModelIds.push_back(id);
    }

    void meshDrawClearSprites()
    {
        for (DrawPrimitive& sprite : _sprites)
        {
            destroyPrimitive(sprite);
        }
        _sprites.clear();
        _spriteIds.clear();
    }

    void meshDrawBeginFrame()
    {
        _spriteIds.nextFrame();
        _distantUsed = 0;
    }

    void meshDrawReleaseSprite(int32_t id)
    {
        if (id < 0 || static_cast<size_t>(id) >= _sprites.size())
        {
            return;
        }
        destroyPrimitive(_sprites[static_cast<size_t>(id)]);
        _spriteIds.release(id);
    }

    uint32_t meshDrawGpuBytes(const CpuMesh& mesh)
    {
        uint64_t bytes = 0;
        for (const auto& primitive : mesh.primitives)
        {
            bytes += static_cast<uint64_t>(primitive.vertices.size()) * Gpu::vertexStride();
            bytes += static_cast<uint64_t>(primitive.indices.size()) * sizeof(uint32_t);
        }
        for (const auto& image : mesh.images)
        {
            bytes += image.rgba.size();
        }
        return static_cast<uint32_t>(std::min(bytes, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
    }

    MeshUpload meshDrawUploadModel(const CpuMesh& mesh)
    {
        if (!meshDrawInit())
        {
            return { MeshUploadStatus::capacity, -1 };
        }
        DrawModel model;
        auto abandon = [&](MeshUploadStatus status = MeshUploadStatus::capacity) {
            for (DrawPrimitive& primitive : model.primitives)
            {
                destroyPrimitive(primitive);
            }
            model.primitives.clear();
            return MeshUpload{ status, -1 };
        };
        for (const MeshPrimitive& primitive : mesh.primitives)
        {
            Gpu::Texture texture;
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
                    return abandon(MeshUploadStatus::rejected);
                }
                const MeshImage& image = mesh.images[static_cast<size_t>(primitive.image)];
                const bool rgbaReady = !image.rgba.empty() && image.width > 0 && image.height > 0
                    && image.rgba.size() >= static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4u;
                if (!rgbaReady)
                {
                    return abandon(MeshUploadStatus::rejected);
                }
                clampUv = image.clamp;
                const uint32_t padded = kAtlasGutter * 2u;
                // Repeat is a dedicated GL_REPEAT texture. An atlas rect cannot wrap
                // an interpolated UV without sampling the neighbour or the gutter.
                const bool atlasFit = image.clamp && image.nearest
                    && image.width + static_cast<int32_t>(padded) <= static_cast<int32_t>(kAtlasSize)
                    && image.height + static_cast<int32_t>(padded) <= static_cast<int32_t>(kAtlasSize);
                if (atlasFit)
                {
                    // A full atlas fails the upload. The caller evicts a model, which
                    // frees that model's rect, and retries. A private texture would
                    // spend a handle instead of reusing the atlas.
                    if (!_atlas.pack(static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height), slot))
                    {
                        return abandon();
                    }
                    packed = true;
                    texture = atlasTexture(slot.page);
                    sharedTexture = texture.handle != nullptr;
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
                        image.nearest, image.clamp);
                    if (texture.handle == nullptr)
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
                else if (texture.handle != nullptr && texture.handle != _white.handle)
                {
                    Gpu::destroy(texture);
                }
                return abandon();
            }
            gpu.inAtlas = sharedTexture;
            gpu.atlasSlot = slot;
            gpu.ownsTexture = !sharedTexture && texture.handle != _white.handle;
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
            return { MeshUploadStatus::rejected, -1 };
        }
        int32_t id = 0;
        if (!_freeModelIds.empty())
        {
            id = _freeModelIds.back();
            _freeModelIds.pop_back();
            _models[static_cast<size_t>(id)] = std::move(model);
        }
        else
        {
            _models.push_back(std::move(model));
            id = static_cast<int32_t>(_models.size() - 1);
        }
        return { MeshUploadStatus::ready, id };
    }

    SpritePlace meshDrawUploadSprite(
        const SpriteQuad& quad, const uint8_t* rgba, int32_t width, int32_t height, bool allowDedicated)
    {
        SpritePlace place;
        if (!meshDrawInit() || rgba == nullptr || width <= 0 || height <= 0 || width > 65535 || height > 65535)
        {
            return place;
        }
        Gpu::Texture texture;
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
            sharedTexture = texture.handle != nullptr;
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
            if (!allowDedicated || estimatedHandles() + 1u >= kHandleCap)
            {
                return place;
            }
            texture = uploadRgba(rgba, static_cast<uint16_t>(width), static_cast<uint16_t>(height), true, true);
        }
        if (texture.handle == nullptr)
        {
            return place;
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
            else if (texture.handle != nullptr)
            {
                Gpu::destroy(texture);
            }
            return place;
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
        const int32_t recycled = _spriteIds.acquire();
        int32_t id;
        if (recycled >= 0)
        {
            id = recycled;
            _sprites[static_cast<size_t>(id)] = gpu;
        }
        else
        {
            _sprites.push_back(gpu);
            id = static_cast<int32_t>(_sprites.size() - 1);
        }
        place.id = id;
        place.page = sharedTexture ? static_cast<int32_t>(slot.page) : -1;
        place.uvScaleX = uvScaleX;
        place.uvScaleY = uvScaleY;
        place.uvOffX = uvOffX;
        place.uvOffY = uvOffY;
        return place;
    }

    void meshDrawDistantSprites(int32_t page, int face, const DistantSpriteInstance* instances, uint32_t count)
    {
        if (instances == nullptr || count == 0 || count > UINT32_MAX / 6 || page < 0 || !meshDrawInit())
        {
            return;
        }
        const Gpu::Texture texture = atlasTexture(static_cast<uint32_t>(page));
        if (texture.handle == nullptr)
        {
            return;
        }
        if (_distantUsed == _distantBatches.size())
        {
            _distantBatches.push_back({});
        }
        auto& batch = _distantBatches[_distantUsed++];
        if (batch.material == nullptr)
        {
            batch.material = Gpu::createMaterial(Gpu::MaterialKind::sprite);
        }
        const uint32_t vertexCount = count * 6;
        if (batch.capacity < vertexCount)
        {
            Gpu::destroy(batch.geometry);
            batch.capacity = 0;
            batch.geometry.vertices = Gpu::createVertices(vertexCount);
            batch.geometry.indices = Gpu::createIndices(vertexCount);
            if (batch.geometry.vertices == nullptr || batch.geometry.indices == nullptr)
            {
                Gpu::destroy(batch.geometry);
                return;
            }
            std::vector<uint32_t> indices(vertexCount);
            for (uint32_t i = 0; i < vertexCount; ++i)
            {
                indices[i] = i;
            }
            Gpu::updateIndices(batch.geometry.indices, indices.data(), vertexCount);
            batch.capacity = vertexCount;
        }
        constexpr float kInvSqrt2 = 0.7071067811865476f;
        const float nx = kSpriteEyeX[face & 3] * kInvSqrt2;
        const float ny = kSpriteEyeY[face & 3] * kInvSqrt2;
        const uint32_t corners[] = { 0, 1, 2, 0, 2, 3 };
        std::vector<Gpu::Vertex> vertices(vertexCount);
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto& sprite = instances[i];
            for (uint32_t j = 0; j < 6; ++j)
            {
                const uint32_t corner = corners[j];
                const bool right = corner == 1 || corner == 2;
                const bool top = corner >= 2;
                const float across = right ? sprite.right : sprite.left;
                vertices[i * 6 + j] = { sprite.x - ny * across, sprite.y + nx * across,
                    sprite.z + (top ? sprite.top : sprite.bottom), nx, ny, 0.0f,
                    sprite.uvOffX + (right ? sprite.uvScaleX : 0.0f),
                    sprite.uvOffY + (top ? 0.0f : sprite.uvScaleY) };
            }
        }
        Gpu::updateVertices(batch.geometry.vertices, vertices.data(), vertexCount);
        batch.geometry.count = vertexCount;
        batch.geometry.bounds = Gpu::bounds(vertices.data(), vertexCount);
        const float uvRect[] = { 1.0f, 1.0f, 0.0f, 0.0f };
        Gpu::configureMesh(batch.material, texture, uvRect, false, 0.5f, false);
        const filament::math::mat4f identity;
        Gpu::draw(batch.geometry, batch.material, &identity, 1, false);
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
