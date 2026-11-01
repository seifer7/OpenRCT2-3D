/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "GlbMesh.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OpenRCT2::View3D
{
    // Bump this when the .o3d bytes change. Older files are rebuilt.
    // Version 2 adds a body checksum, the GLB converter version, and MeshLoadLimits.
    constexpr uint32_t kModelCacheVersion = 2;
    constexpr size_t kModelCacheHeaderBytes = 68;
    // Offsets of the converter version and the first MeshLoadLimits field (maxVertices).
    constexpr size_t kModelCacheConverterOffset = 40;
    constexpr size_t kModelCacheMaxVerticesOffset = 44;

    struct ModelCacheStats
    {
        int32_t converted = 0;
        int32_t skipped = 0;
        int32_t failed = 0;
    };

    // done is 1-based. total is the number of .glb files that will be considered.
    using ModelCacheProgressFn = void (*)(int32_t done, int32_t total, std::string_view name);

    // sourceDirs is searched in order. The first directory that has an identifier wins.
    // Cache files are <cacheDir>/<identifier>.o3d. A bad GLB is counted and skipped.
    ModelCacheStats prepareModelCache(
        const std::vector<std::string>& sourceDirs, const std::string& cacheDir, ModelCacheProgressFn progress);

    // Loads a converted mesh. When the cache header's source size and modification
    // time still match the GLB, the file is read once and its body checksum is checked.
    // The GLB is hashed only when that size or time differs. A missing or bad cache
    // parses the GLB. `error` is empty when the file is simply absent.
    std::optional<CpuMesh> loadModelForDraw(
        const std::string& identifier, const std::vector<std::string>& sourceDirs, const std::string& cacheDir,
        std::string& error);
} // namespace OpenRCT2::View3D
