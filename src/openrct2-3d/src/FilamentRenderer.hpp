/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <filament/Box.h>
#include <math/mat4.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace filament
{
	class Engine;
	class IndexBuffer;
	class MaterialInstance;
	class Texture;
	class VertexBuffer;
}

namespace OpenRCT2::View3D::Gpu
{
	struct Vertex
	{
		float x, y, z;
		float nx, ny, nz;
		float u, v;
		uint8_t red = 255, green = 255, blue = 255, alpha = 255;
	};

	struct Texture
	{
		filament::Texture* handle = nullptr;
		bool nearest = true;
		bool clamp = true;
		bool singleChannel = false;
	};

	struct Geometry
	{
		filament::VertexBuffer* vertices = nullptr;
		filament::IndexBuffer* indices = nullptr;
		uint32_t offset = 0;
		uint32_t count = 0;
		filament::Box bounds;
	};

	enum class MaterialKind : uint8_t
	{
		opaque,
		masked,
		blended,
		sprite,
		terrain,
		water,
	};

	bool initialize(bool noOp = false);
	void flush();
	bool ready();
	void shutdown();
	filament::Engine* engine();
	void beginScene();
	void cameraMatrices(const float view[16], const float projection[16],
		filament::math::mat4f& model, filament::math::mat4f& adjustedProjection);
	void setCamera(const float view[16], const float projection[16]);
	bool render(uint32_t width, uint32_t height, uint8_t* rgba);

	Texture createTexture(uint32_t width, uint32_t height, uint32_t layers = 1, bool nearest = true,
		bool clamp = true, bool singleChannel = false);
	void updateTexture(Texture texture, const uint8_t* pixels, uint32_t width, uint32_t height,
		uint32_t x = 0, uint32_t y = 0, uint32_t layer = 0);
	void destroy(Texture& texture);

	filament::VertexBuffer* createVertices(uint32_t count);
	uint32_t vertexStride();
	filament::IndexBuffer* createIndices(uint32_t count);
	void updateVertices(filament::VertexBuffer* buffer, const Vertex* vertices, uint32_t count, uint32_t offset = 0);
	void updateIndices(filament::IndexBuffer* buffer, const uint32_t* indices, uint32_t count, uint32_t offset = 0);
	void destroy(filament::VertexBuffer*& buffer);
	void destroy(filament::IndexBuffer*& buffer);
	bool uploadGeometry(Geometry& geometry, const Vertex* vertices, uint32_t count);
	void destroy(Geometry& geometry);
	filament::Box bounds(const Vertex* vertices, uint32_t count);

	filament::MaterialInstance* createMaterial(MaterialKind kind);
	void setTexture(filament::MaterialInstance* material, const char* name, Texture texture);
	void setFloat4(filament::MaterialInstance* material, const char* name, const float values[4]);
	void configureMesh(filament::MaterialInstance* material, Texture texture, const float uvRect[4],
		bool clamp, float cutoff, bool doubleSided);
	void destroy(filament::MaterialInstance*& material);
	void draw(const Geometry& geometry, filament::MaterialInstance* material,
		const filament::math::mat4f* transforms, uint32_t count, bool shadows = true);
	filament::math::mat4f instanceTransform(float x, float y, float z, float yaw);
}
