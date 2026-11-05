/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "FilamentRenderer.hpp"

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/IndirectLight.h>
#include <filament/InstanceBuffer.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/RenderTarget.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/SwapChain.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <math/mat3.h>
#include <utils/EntityManager.h>

#include "view3d_materials.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace OpenRCT2::View3D::Gpu
{
    namespace
    {
        using namespace filament;
        using namespace filament::math;

        struct PackedVertex
        {
            float3 position;
            quatf tangent;
            float2 uv;
            float2 tile;
            uint8_t colour[4];
        };

        struct Batch
        {
            utils::Entity entity;
            InstanceBuffer* instances = nullptr;
            Geometry geometry;
            MaterialInstance* material = nullptr;
            bool built = false;
            bool shadows = true;
        };

        Engine* _engine = nullptr;
        Renderer* _renderer = nullptr;
        Scene* _scene = nullptr;
        View* _view = nullptr;
        Camera* _camera = nullptr;
        utils::Entity _cameraEntity;
        utils::Entity _sun;
        IndirectLight* _ambient = nullptr;
        Skybox* _sky = nullptr;
        filament::Texture* _environment = nullptr;
        SwapChain* _swapChain = nullptr;
        RenderTarget* _target = nullptr;
        filament::Texture* _colour = nullptr;
        filament::Texture* _depth = nullptr;
        std::array<Material*, 6> _materials{};
        std::vector<Batch> _batches;
        size_t _usedBatches = 0;
        uint32_t _width = 0;
        uint32_t _height = 0;
        std::vector<uint8_t> _pixels;
        bool _readComplete = false;

        void freeBuffer(void* buffer, size_t, void*)
        {
            std::free(buffer);
        }

        void* copyBytes(const void* source, size_t size)
        {
            void* data = std::malloc(size);
            if (data == nullptr)
            {
                throw std::bad_alloc();
            }
            std::memcpy(data, source, size);
            return data;
        }

        void invalidate(Batch& batch)
        {
            if (batch.built)
            {
                _scene->remove(batch.entity);
                _engine->getRenderableManager().destroy(batch.entity);
                batch.built = false;
            }
        }

        template<typename T> void destroyResource(T*& resource)
        {
            if (_engine != nullptr && resource != nullptr)
            {
                _engine->destroy(resource);
            }
            resource = nullptr;
        }

        void releaseTarget()
        {
            destroyResource(_target);
            destroyResource(_colour);
            destroyResource(_depth);
            destroyResource(_swapChain);
            _width = _height = 0;
        }

        bool ensureTarget(uint32_t width, uint32_t height)
        {
            if (_target != nullptr && _width == width && _height == height)
            {
                return true;
            }
            _engine->flushAndWait();
            _view->setRenderTarget(nullptr);
            releaseTarget();
            _swapChain = _engine->createSwapChain(width, height);
            _colour = filament::Texture::Builder().width(width).height(height).levels(1)
                .format(filament::Texture::InternalFormat::RGBA8)
                .usage(filament::Texture::Usage::COLOR_ATTACHMENT | filament::Texture::Usage::SAMPLEABLE
                    | filament::Texture::Usage::BLIT_SRC)
                .build(*_engine);
            _depth = filament::Texture::Builder().width(width).height(height).levels(1)
                .format(filament::Texture::InternalFormat::DEPTH24)
                .usage(filament::Texture::Usage::DEPTH_ATTACHMENT).build(*_engine);
            if (_swapChain == nullptr || _colour == nullptr || _depth == nullptr)
            {
                releaseTarget();
                return false;
            }
            _target = RenderTarget::Builder().texture(RenderTarget::AttachmentPoint::COLOR, _colour)
                .texture(RenderTarget::AttachmentPoint::DEPTH, _depth).build(*_engine);
            if (_target == nullptr)
            {
                releaseTarget();
                return false;
            }
            _view->setRenderTarget(_target);
            _view->setViewport({ 0, 0, width, height });
            _width = width;
            _height = height;
            _pixels.resize(static_cast<size_t>(width) * height * 4);
            return true;
        }
    }

    bool initialize(bool noOp)
    {
        if (ready())
        {
            return true;
        }
#if defined(__APPLE__)
        constexpr Engine::Backend backend = Engine::Backend::METAL;
#else
        constexpr Engine::Backend backend = Engine::Backend::OPENGL;
#endif
        _engine = Engine::create(noOp ? Engine::Backend::NOOP : backend);
        if (_engine == nullptr)
        {
            return false;
        }
        const void* packages[] = { VIEW3D_MATERIALS_MESH_OPAQUE_DATA, VIEW3D_MATERIALS_MESH_MASKED_DATA,
            VIEW3D_MATERIALS_MESH_BLEND_DATA, VIEW3D_MATERIALS_SPRITE_DATA,
            VIEW3D_MATERIALS_TERRAIN_DATA, VIEW3D_MATERIALS_WATER_DATA };
        const size_t sizes[] = { VIEW3D_MATERIALS_MESH_OPAQUE_SIZE, VIEW3D_MATERIALS_MESH_MASKED_SIZE,
            VIEW3D_MATERIALS_MESH_BLEND_SIZE, VIEW3D_MATERIALS_SPRITE_SIZE,
            VIEW3D_MATERIALS_TERRAIN_SIZE, VIEW3D_MATERIALS_WATER_SIZE };
        for (size_t i = 0; i < _materials.size(); ++i)
        {
            _materials[i] = Material::Builder().package(packages[i], sizes[i]).build(*_engine);
            if (_materials[i] == nullptr)
            {
                shutdown();
                return false;
            }
        }
        _renderer = _engine->createRenderer();
        _scene = _engine->createScene();
        _view = _engine->createView();
        _cameraEntity = utils::EntityManager::get().create();
        _camera = _engine->createCamera(_cameraEntity);
        _camera->setExposure(16.0f, 1.0f / 125.0f, 100.0f);
        _view->setScene(_scene);
        _view->setCamera(_camera);
        AmbientOcclusionOptions ao;
        ao.enabled = true;
        ao.radius = 1.0f;
        _view->setAmbientOcclusionOptions(ao);
        ScreenSpaceReflectionsOptions reflections;
        reflections.enabled = true;
        _view->setScreenSpaceReflectionsOptions(reflections);
        _view->setAntiAliasing(AntiAliasing::FXAA);
        _view->setShadowingEnabled(true);
        _renderer->setClearOptions({ .clearColor = { 0.435f, 0.659f, 0.863f, 1.0f }, .clear = true });

        _sun = utils::EntityManager::get().create();
        LightManager::ShadowOptions shadows;
        shadows.mapSize = 2048;
        shadows.shadowCascades = 3;
        shadows.shadowFar = 150.0f;
        LightManager::Builder(LightManager::Type::SUN).direction({ 0.35f, 0.45f, -0.82f })
            .color({ 1.0f, 0.96f, 0.9f }).intensity(100000.0f).castShadows(true).shadowOptions(shadows)
            .build(*_engine, _sun);
        _scene->addEntity(_sun);

        _environment = filament::Texture::Builder().width(1).height(1).levels(1)
            .sampler(filament::Texture::Sampler::SAMPLER_CUBEMAP)
            .format(filament::Texture::InternalFormat::RGBA8).build(*_engine);
        const uint8_t skyPixels[] = { 140, 175, 220, 255, 140, 175, 220, 255, 120, 165, 230, 255,
            80, 100, 70, 255, 145, 180, 225, 255, 145, 180, 225, 255 };
        _environment->setImage(*_engine, 0, 0, 0, 0, 1, 1, 6,
            filament::Texture::PixelBufferDescriptor(copyBytes(skyPixels, sizeof(skyPixels)), sizeof(skyPixels),
                filament::Texture::Format::RGBA, filament::Texture::Type::UBYTE, freeBuffer));
        const float3 irradiance[] = { { 0.8f, 0.9f, 1.0f } };
        _ambient = IndirectLight::Builder().irradiance(1, irradiance).reflections(_environment)
            .intensity(30000.0f).build(*_engine);
        _scene->setIndirectLight(_ambient);
        _sky = Skybox::Builder().color({ 0.435f, 0.659f, 0.863f, 1.0f }).build(*_engine);
        _scene->setSkybox(_sky);
        return true;
    }

    bool ready()
    {
        return _engine != nullptr;
    }

    void flush()
    {
        if (ready())
        {
            _engine->flushAndWait();
        }
    }

    filament::Engine* engine()
    {
        return _engine;
    }

    void beginScene()
    {
        if (_scene == nullptr)
        {
            return;
        }
        for (auto& batch : _batches)
        {
            _scene->remove(batch.entity);
        }
        _usedBatches = 0;
    }

    void shutdown()
    {
        if (_engine == nullptr)
        {
            return;
        }
        _engine->flushAndWait();
        for (auto& batch : _batches)
        {
            invalidate(batch);
            destroyResource(batch.instances);
            _engine->destroy(batch.entity);
            utils::EntityManager::get().destroy(batch.entity);
        }
        _batches.clear();
        _usedBatches = 0;
        destroyResource(_view);
        releaseTarget();
        destroyResource(_scene);
        destroyResource(_sky);
        destroyResource(_ambient);
        destroyResource(_environment);
        if (_sun)
        {
            _engine->destroy(_sun);
            utils::EntityManager::get().destroy(_sun);
            _sun = {};
        }
        if (_cameraEntity)
        {
            _engine->destroyCameraComponent(_cameraEntity);
            utils::EntityManager::get().destroy(_cameraEntity);
            _cameraEntity = {};
        }
        _camera = nullptr;
        destroyResource(_renderer);
        for (auto*& material : _materials)
        {
            destroyResource(material);
        }
        Engine::destroy(&_engine);
        _pixels.clear();
    }

    void cameraMatrices(const float view[16], const float projection[16], mat4f& model, mat4f& adjustedProjection)
    {
        mat4f v;
        mat4f p;
        std::memcpy(&v, view, sizeof(v));
        std::memcpy(&p, projection, sizeof(p));
        // The existing camera uses positive view-space Z. Filament requires negative Z;
        // flip both factors so projection * view and CPU visibility remain unchanged.
        for (size_t column = 0; column < 4; ++column)
        {
            v[column][2] = -v[column][2];
        }
        p[2] = -p[2];
        model = inverse(v);
        adjustedProjection = p;
    }

    void setCamera(const float view[16], const float projection[16])
    {
        if (_camera == nullptr)
        {
            return;
        }
        mat4f model;
        mat4f adjustedProjection;
        cameraMatrices(view, projection, model, adjustedProjection);
        _camera->setModelMatrix(model);
        _camera->setCustomProjection(mat4(adjustedProjection), 0.2, 8000.0);
    }

    bool render(uint32_t width, uint32_t height, uint8_t* rgba)
    {
        if (!ready() || rgba == nullptr || width == 0 || height == 0 || !ensureTarget(width, height))
        {
            return false;
        }
        if (!_renderer->beginFrame(_swapChain))
        {
            return false;
        }
        _renderer->render(_view);
        _readComplete = false;
        _renderer->readPixels(_target, 0, 0, width, height,
            backend::PixelBufferDescriptor(_pixels.data(), _pixels.size(), backend::PixelDataFormat::RGBA,
                backend::PixelDataType::UBYTE, [](void*, size_t, void*) { _readComplete = true; }));
        _renderer->endFrame();
        // The existing presenter consumes a complete frame synchronously. Do not expose
        // the readback memory until Filament has finished and dispatched its callback.
        _engine->flushAndWait();
        if (!_readComplete)
        {
            return false;
        }
        std::memcpy(rgba, _pixels.data(), _pixels.size());
        return true;
    }

    Texture createTexture(uint32_t width, uint32_t height, uint32_t layers, bool nearest, bool clamp, bool singleChannel)
    {
        Texture result;
        if (!ready() || width == 0 || height == 0 || layers == 0)
        {
            return result;
        }
        result.handle = filament::Texture::Builder().width(width).height(height).depth(layers).levels(1)
            .sampler(layers > 1 ? filament::Texture::Sampler::SAMPLER_2D_ARRAY : filament::Texture::Sampler::SAMPLER_2D)
            .format(singleChannel ? filament::Texture::InternalFormat::R8 : filament::Texture::InternalFormat::SRGB8_A8)
            .build(*_engine);
        result.nearest = nearest;
        result.clamp = clamp;
        result.singleChannel = singleChannel;
        return result;
    }

    void updateTexture(Texture texture, const uint8_t* pixels, uint32_t width, uint32_t height,
        uint32_t x, uint32_t y, uint32_t layer)
    {
        if (!ready() || texture.handle == nullptr || pixels == nullptr || width == 0 || height == 0)
        {
            return;
        }
        const size_t size = static_cast<size_t>(width) * height * (texture.singleChannel ? 1 : 4);
        texture.handle->setImage(*_engine, 0, x, y, layer, width, height, 1,
            filament::Texture::PixelBufferDescriptor(copyBytes(pixels, size), size,
                texture.singleChannel ? filament::Texture::Format::R : filament::Texture::Format::RGBA,
                filament::Texture::Type::UBYTE, 1, 0, 0, 0, freeBuffer));
    }

    void destroy(Texture& texture)
    {
        destroyResource(texture.handle);
        texture = {};
    }

    uint32_t vertexStride()
    {
        return static_cast<uint32_t>(sizeof(PackedVertex));
    }

    filament::VertexBuffer* createVertices(uint32_t count)
    {
        if (!ready() || count == 0)
        {
            return nullptr;
        }
        using Attribute = VertexBuffer::AttributeType;
        return VertexBuffer::Builder().vertexCount(count).bufferCount(1)
            .attribute(VertexAttribute::POSITION, 0, Attribute::FLOAT3, offsetof(PackedVertex, position), sizeof(PackedVertex))
            .attribute(VertexAttribute::TANGENTS, 0, Attribute::FLOAT4, offsetof(PackedVertex, tangent), sizeof(PackedVertex))
            .attribute(VertexAttribute::UV0, 0, Attribute::FLOAT2, offsetof(PackedVertex, uv), sizeof(PackedVertex))
            .attribute(VertexAttribute::UV1, 0, Attribute::FLOAT2, offsetof(PackedVertex, tile), sizeof(PackedVertex))
            .attribute(VertexAttribute::COLOR, 0, Attribute::UBYTE4, offsetof(PackedVertex, colour), sizeof(PackedVertex))
            .normalized(VertexAttribute::COLOR).build(*_engine);
    }

    filament::IndexBuffer* createIndices(uint32_t count)
    {
        return ready() && count > 0 ? IndexBuffer::Builder().indexCount(count)
            .bufferType(IndexBuffer::IndexType::UINT).build(*_engine) : nullptr;
    }

    void updateVertices(filament::VertexBuffer* buffer, const Vertex* vertices, uint32_t count, uint32_t offset)
    {
        if (!ready() || buffer == nullptr || vertices == nullptr || count == 0)
        {
            return;
        }
        std::vector<PackedVertex> packed(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto& v = vertices[i];
            float3 n{ v.nx, v.ny, v.nz };
            n = dot(n, n) > 0.000001f ? normalize(n) : float3{ 0, 0, 1 };
            const float3 axis = std::abs(n.z) < 0.99f ? float3{ 0, 0, 1 } : float3{ 0, 1, 0 };
            const float3 tangent = normalize(cross(axis, n));
            packed[i] = { { v.x, v.y, v.z }, mat3f::packTangentFrame(mat3f{ tangent, cross(n, tangent), n }),
                { v.u, v.v }, { v.x, v.y }, { v.red, v.green, v.blue, v.alpha } };
        }
        const size_t size = packed.size() * sizeof(PackedVertex);
        buffer->setBufferAt(*_engine, 0, VertexBuffer::BufferDescriptor(copyBytes(packed.data(), size), size, freeBuffer),
            offset * static_cast<uint32_t>(sizeof(PackedVertex)));
    }

    void updateIndices(filament::IndexBuffer* buffer, const uint32_t* indices, uint32_t count, uint32_t offset)
    {
        if (!ready() || buffer == nullptr || indices == nullptr || count == 0)
        {
            return;
        }
        const size_t size = static_cast<size_t>(count) * sizeof(uint32_t);
        buffer->setBuffer(*_engine, IndexBuffer::BufferDescriptor(copyBytes(indices, size), size, freeBuffer),
            offset * static_cast<uint32_t>(sizeof(uint32_t)));
    }

    void destroy(filament::VertexBuffer*& buffer)
    {
        for (auto& batch : _batches)
        {
            if (batch.geometry.vertices == buffer)
            {
                invalidate(batch);
            }
        }
        destroyResource(buffer);
    }

    void destroy(filament::IndexBuffer*& buffer)
    {
        for (auto& batch : _batches)
        {
            if (batch.geometry.indices == buffer)
            {
                invalidate(batch);
            }
        }
        destroyResource(buffer);
    }

    filament::Box bounds(const Vertex* vertices, uint32_t count)
    {
        if (vertices == nullptr || count == 0)
        {
            return { { 0, 0, 0 }, { 0.01f, 0.01f, 0.01f } };
        }
        float3 low{ vertices[0].x, vertices[0].y, vertices[0].z };
        float3 high = low;
        for (uint32_t i = 1; i < count; ++i)
        {
            const float3 p{ vertices[i].x, vertices[i].y, vertices[i].z };
            low = min(low, p);
            high = max(high, p);
        }
        return { (low + high) * 0.5f, max((high - low) * 0.5f, float3{ 0.01f }) };
    }

    bool uploadGeometry(Geometry& geometry, const Vertex* vertices, uint32_t count)
    {
        destroy(geometry);
        if (count == 0)
        {
            return true;
        }
        geometry.vertices = createVertices(count);
        geometry.indices = createIndices(count);
        if (geometry.vertices == nullptr || geometry.indices == nullptr)
        {
            destroy(geometry);
            return false;
        }
        std::vector<uint32_t> indices(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            indices[i] = i;
        }
        updateVertices(geometry.vertices, vertices, count);
        updateIndices(geometry.indices, indices.data(), count);
        geometry.count = count;
        geometry.bounds = bounds(vertices, count);
        return true;
    }

    void destroy(Geometry& geometry)
    {
        destroy(geometry.vertices);
        destroy(geometry.indices);
        geometry = {};
    }

    filament::MaterialInstance* createMaterial(MaterialKind kind)
    {
        return ready() ? _materials[static_cast<size_t>(kind)]->createInstance() : nullptr;
    }

    void setTexture(filament::MaterialInstance* material, const char* name, Texture texture)
    {
        if (material == nullptr || texture.handle == nullptr)
        {
            return;
        }
        TextureSampler sampler(texture.nearest ? TextureSampler::MinFilter::NEAREST : TextureSampler::MinFilter::LINEAR,
            texture.nearest ? TextureSampler::MagFilter::NEAREST : TextureSampler::MagFilter::LINEAR,
            texture.clamp ? TextureSampler::WrapMode::CLAMP_TO_EDGE : TextureSampler::WrapMode::REPEAT);
        material->setParameter(name, texture.handle, sampler);
    }

    void setFloat4(filament::MaterialInstance* material, const char* name, const float values[4])
    {
        if (material != nullptr)
        {
            material->setParameter(name, float4{ values[0], values[1], values[2], values[3] });
        }
    }

    void configureMesh(filament::MaterialInstance* material, Texture texture, const float uvRect[4],
        bool clamp, float cutoff, bool doubleSided)
    {
        if (material == nullptr)
        {
            return;
        }
        setTexture(material, "albedo", texture);
        setFloat4(material, "uvRect", uvRect);
        material->setParameter("clampUv", clamp);
        if (material->getMaterial()->getBlendingMode() == filament::BlendingMode::MASKED)
        {
            material->setMaskThreshold(cutoff);
        }
        material->setDoubleSided(doubleSided);
        material->setCullingMode(doubleSided ? MaterialInstance::CullingMode::NONE : MaterialInstance::CullingMode::BACK);
    }

    void destroy(filament::MaterialInstance*& material)
    {
        for (auto& batch : _batches)
        {
            if (batch.material == material)
            {
                invalidate(batch);
            }
        }
        destroyResource(material);
    }

    void draw(const Geometry& geometry, filament::MaterialInstance* material, const filament::math::mat4f* transforms,
        uint32_t count, bool shadows)
    {
        if (!ready() || geometry.vertices == nullptr || geometry.indices == nullptr || geometry.count == 0
            || material == nullptr || transforms == nullptr || count == 0)
        {
            return;
        }
        const auto capacity = static_cast<uint32_t>(_engine->getMaxAutomaticInstances());
        auto& manager = _engine->getRenderableManager();
        uint32_t drawn = 0;
        while (drawn < count)
        {
            const uint32_t size = std::min(capacity, count - drawn);
            if (_usedBatches == _batches.size())
            {
                Batch batch;
                batch.entity = utils::EntityManager::get().create();
                batch.instances = InstanceBuffer::Builder(capacity).build(*_engine);
                _batches.push_back(batch);
            }
            auto& batch = _batches[_usedBatches++];
            if (batch.built && (batch.geometry.vertices != geometry.vertices || batch.geometry.indices != geometry.indices
                || batch.geometry.offset != geometry.offset || batch.geometry.count != geometry.count
                || batch.material != material || batch.shadows != shadows))
            {
                invalidate(batch);
            }
            batch.instances->setLocalTransforms(transforms + drawn, size);
            float3 low{ std::numeric_limits<float>::max() };
            float3 high{ std::numeric_limits<float>::lowest() };
            for (uint32_t i = 0; i < size; ++i)
            {
                for (uint32_t corner = 0; corner < 8; ++corner)
                {
                    const float3 sign{ (corner & 1) ? 1.0f : -1.0f, (corner & 2) ? 1.0f : -1.0f,
                        (corner & 4) ? 1.0f : -1.0f };
                    const float3 position = (transforms[drawn + i]
                        * float4{ geometry.bounds.center + geometry.bounds.halfExtent * sign, 1.0f }).xyz;
                    low = min(low, position);
                    high = max(high, position);
                }
            }
            const filament::Box instanceBounds{ (low + high) * 0.5f, (high - low) * 0.5f };
            if (!batch.built)
            {
                RenderableManager::Builder(1).boundingBox(instanceBounds).culling(false)
                    .geometry(0, RenderableManager::PrimitiveType::TRIANGLES,
                        geometry.vertices, geometry.indices, geometry.offset, geometry.count)
                    .material(0, material).instances(size, batch.instances).castShadows(shadows).receiveShadows(shadows)
                    .build(*_engine, batch.entity);
                batch.geometry = geometry;
                batch.material = material;
                batch.shadows = shadows;
                batch.built = true;
            }
            else
            {
                manager.setInstanceCount(manager.getInstance(batch.entity), size);
                manager.setAxisAlignedBoundingBox(manager.getInstance(batch.entity), instanceBounds);
            }
            _scene->addEntity(batch.entity);
            drawn += size;
        }
    }

    filament::math::mat4f instanceTransform(float x, float y, float z, float yaw)
    {
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        return { float4{ c, s, 0, 0 }, float4{ -s, c, 0, 0 }, float4{ 0, 0, 1, 0 }, float4{ x, y, z, 1 } };
    }
}
