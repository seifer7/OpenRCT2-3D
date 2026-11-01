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
    // Bump this when the cache bytes change. Older files are rebuilt.
    constexpr uint32_t kModelCacheVersion = 1;

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

    // Loads a converted mesh. Uses the cache when the GLB's size, modification time,
    // and content hash still match. Parses the GLB when the cache is missing or unreadable.
    // Returns nullopt for a missing or unusable GLB. `error` is empty when the file is simply absent.
    std::optional<CpuMesh> loadModelForDraw(
        const std::string& identifier, const std::vector<std::string>& sourceDirs, const std::string& cacheDir,
        std::string& error);
} // namespace OpenRCT2::View3D
