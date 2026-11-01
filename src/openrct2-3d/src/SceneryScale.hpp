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

namespace OpenRCT2::View3D
{
    // 16x16 tiles. A 256-tile map is 256 chunks.
    constexpr int32_t kSceneryChunkTiles = 16;
    // Chunks whose centre is farther than this, in tiles, are not drawn.
    constexpr float kDefaultViewDistanceTiles = 48.0f;
    // GPU bytes for scenery meshes and their textures. LRU drops the coldest model.
    constexpr uint32_t kDefaultVramBudgetBytes = 256u * 1024u * 1024u;
    // Shared geometry slabs. Two handles (vertex + index) per slab.
    constexpr uint32_t kSlabVertices = 65535u;
    constexpr uint32_t kSlabIndices = 262144u;
    constexpr uint32_t kMaxGeometrySlabs = 32u;
    // Atlas pages plus the 1x1 white texture. Well under bgfx's 4096-handle cap.
    constexpr uint32_t kAtlasSize = 2048u;
    constexpr uint32_t kMaxAtlasPages = 8u;
    constexpr uint32_t kUploadsPerFrame = 2u;
    constexpr uint32_t kUploadBytesPerFrame = 1024u * 1024u;
    constexpr uint32_t kHandleCap = 4096u;
} // namespace OpenRCT2::View3D
