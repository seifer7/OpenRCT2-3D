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
#include <optional>
#include <string>
#include <vector>

namespace OpenRCT2::View3D
{
    struct MeshVertex
    {
        float x;
        float y;
        float z;
        float nx;
        float ny;
        float nz;
        float u;
        float v;
    };

    enum class MeshAlpha : uint8_t
    {
        opaque,
        mask,
        blend,
    };

    struct MeshImage
    {
        // PNG bytes from the GLB. rgba is set instead when the caller already decoded.
        std::vector<uint8_t> png;
        std::vector<uint8_t> rgba;
        uint32_t width = 0;
        uint32_t height = 0;
        bool nearest = false;
        bool clamp = false;
    };

    struct MeshPrimitive
    {
        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;
        MeshAlpha alpha = MeshAlpha::opaque;
        float alphaCutoff = 0.0f;
        bool doubleSided = false;
        int32_t image = -1;
    };

    struct MeshBounds
    {
        float minX = 0.0f;
        float minY = 0.0f;
        float minZ = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        float maxZ = 0.0f;
    };

    struct CpuMesh
    {
        std::vector<MeshPrimitive> primitives;
        std::vector<MeshImage> images;
        MeshBounds bounds;
    };

    struct MeshLoadLimits
    {
        uint32_t maxVertices = 65535;
        uint32_t maxIndices = 200000;
        uint32_t maxPrimitives = 8;
        uint32_t maxImageBytes = 16u * 1024u * 1024u;
    };

    // Positions and normals are converted into viewer space. Image bytes stay PNG.
    // `error` is a short reason when this returns nullopt.
    std::optional<CpuMesh> loadGlbMesh(const uint8_t* data, size_t size, const MeshLoadLimits& limits, std::string& error);

    std::optional<CpuMesh> loadGlbFile(const std::string& path, const MeshLoadLimits& limits, std::string& error);
} // namespace OpenRCT2::View3D
