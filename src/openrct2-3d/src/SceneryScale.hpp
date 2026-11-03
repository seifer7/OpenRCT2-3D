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
    // sceneryViewDistanceTiles() is the live value. Tests and tools can change it.
    constexpr float kDefaultViewDistanceTiles = 48.0f;
    // Dirty chunks rebuilt on one frame. The rest stay dirty for later frames.
    constexpr int32_t kChunkRebuildsPerFrame = 8;
    // Empty pixels around an atlas image so a uv of 1 does not sample the neighbour.
    constexpr uint32_t kAtlasGutter = 2u;
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
    // Distant fill-in. Near X-sprites ignore this and kMaxSpriteImages: a face inside
    // the 3D radius is drawn even when the distant layer has filled both.
    constexpr uint32_t kSpriteUploadsPerFrame = 16u;
    // Sprites may use this many atlas pages. Models keep the rest, and a sprite
    // that does not fit does not take a private texture once the dedicated cap is hit.
    constexpr uint32_t kSpriteAtlasPageBudget = 2u;
    // Image cap for the shared sprite set. Near faces may pass it. Distant sprites
    // only take a slot when the count is still under this after the near faces.
    constexpr uint32_t kMaxSpriteImages = 384u;
    constexpr uint32_t kMaxSpriteAtlasTexels = kAtlasSize * kAtlasSize * kSpriteAtlasPageBudget;
    constexpr uint32_t kMaxSpriteDedicated = 32u;
    // Drop a sprite that has not been drawn for this many frames.
    constexpr uint32_t kSpriteStaleFrames = 120u;
    constexpr uint32_t kUploadBytesPerFrame = 1024u * 1024u;
    constexpr uint32_t kHandleCap = 4096u;
    // Stay this far under the cap so terrain, UI, and a burst of sprites still fit.
    constexpr uint32_t kHandleHeadroom = 256u;
    // On-screen ground span. The eye-to-focus distance at high zoom is larger than the view.
    constexpr float kMaxCullTiles = 96.0f;
    // Sprite layer past the 3D radius. The map's far corner cuts this further, so a
    // normal park never walks the horizon. 1280 covers the top of a 1080p zoom-3
    // frame (about 1060 tiles) and stops short of a 4K zoom-3 horizon.
    constexpr float kDefaultDistantSpriteTiles = 1280.0f;

    float sceneryViewDistanceTiles();
    void setSceneryViewDistanceTiles(float tiles);
    float sceneryDistantSpriteTiles();
    void setSceneryDistantSpriteTiles(float tiles);
} // namespace OpenRCT2::View3D
