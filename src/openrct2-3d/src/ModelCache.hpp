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
    // Version 3 checksums every byte except the checksum field, including the
    // primitive and image counts, and rejects trailing bytes.
    constexpr uint32_t kModelCacheVersion = 3;
    constexpr size_t kModelCacheHeaderBytes = 68;
    constexpr size_t kModelCacheSizeOffset = 8;
    constexpr size_t kModelCacheMtimeOffset = 16;
    constexpr size_t kModelCacheHashOffset = 24;
    constexpr size_t kModelCacheChecksumOffset = 32;
    // Offsets of the converter version, the first MeshLoadLimits field, and the primitive count.
    constexpr size_t kModelCacheConverterOffset = 40;
    constexpr size_t kModelCacheMaxVerticesOffset = 44;
    constexpr size_t kModelCachePrimitiveCountOffset = 60;

    // JSON object ids keep the case they were authored with. Official ids are lowercase
    // (`rct2.scenery_small.tcrp`); custom ids may use A-Z (`MyTree`). Allowed characters
    // are [A-Za-z0-9._-], with no leading or trailing dot. Windows device names are rejected.
    bool identifierIsSafe(std::string_view identifier);

    // FNV-1a of every byte except the checksum field. Same function the cache writer uses.
    uint64_t cacheFileChecksum(const uint8_t* data, size_t size);
    // Nanoseconds since the Unix epoch, via File::GetLastModifiedNs.
    uint64_t cacheSourceModifiedNs(const std::string& path);

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

    // True after prepareModelCache has accepted this id. Lookup keeps the authored case.
    bool modelIsValidated(std::string_view identifier);

    // In-game load of a cache that startup already validated. Does not re-check the
    // checksum, version, converter, size, or modification time, and does not read the GLB.
    // An id startup did not accept returns nullopt with an empty error (draw the sprite).
    // A missing, short, or absurdly large file returns nullopt and sets `error` (still the
    // sprite; this is I/O handling). Counts are still checked against the bytes actually
    // read and against MeshLoadLimits so a file replaced after startup cannot over-read
    // or force a huge allocation. That is a memory-safety guard, not validation.
    std::optional<CpuMesh> loadModelForDraw(
        const std::string& identifier, const std::vector<std::string>& sourceDirs, const std::string& cacheDir,
        std::string& error);

    // Streaming FNV-1a of a cache file, skipping the checksum field. One pass per call.
    bool cacheChecksumMatchesFile(const std::string& path);
    uint64_t cacheChecksumPassCount();
    void resetCacheChecksumPassCount();
} // namespace OpenRCT2::View3D
