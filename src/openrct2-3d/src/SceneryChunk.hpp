/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "SceneryScale.hpp"

#include <cstdint>
#include <vector>

namespace OpenRCT2::View3D
{
    struct ChunkPlacement
    {
        uint32_t typeKey = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float yaw = 0.0f;
        bool vegetation = false;
        bool modelCandidate = false;
        int32_t spriteHandles[4] = { -1, -1, -1, -1 };
    };

    struct ChunkBox
    {
        float minX = 0.0f;
        float minY = 0.0f;
        float minZ = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        float maxZ = 0.0f;
    };

    struct SceneryChunk
    {
        bool dirty = true;
        bool built = false;
        ChunkBox box;
        std::vector<ChunkPlacement> placements;
    };

    class SceneryChunkGrid
    {
    public:
        void reset(int32_t tilesX, int32_t tilesY);
        void markAllDirty();
        void markTile(int32_t tileX, int32_t tileY);
        int32_t chunksX() const
        {
            return _chunksX;
        }
        int32_t chunksY() const
        {
            return _chunksY;
        }
        int32_t tilesX() const
        {
            return _tilesX;
        }
        int32_t tilesY() const
        {
            return _tilesY;
        }
        bool dirty(int32_t chunkX, int32_t chunkY) const;
        SceneryChunk* at(int32_t chunkX, int32_t chunkY);
        const SceneryChunk* at(int32_t chunkX, int32_t chunkY) const;
        int32_t dirtyCount() const;

    private:
        int32_t index(int32_t chunkX, int32_t chunkY) const;
        int32_t _tilesX = 0;
        int32_t _tilesY = 0;
        int32_t _chunksX = 0;
        int32_t _chunksY = 0;
        std::vector<SceneryChunk> _chunks;
    };

    void extractFrustumPlanes(const float clip[16], float planes[6][4]);
    bool boxOutsideFrustum(const ChunkBox& box, const float planes[6][4]);
    bool boxOutsideDistance(const ChunkBox& box, float eyeX, float eyeY, float eyeZ, float maxDistanceTiles);

    // Builds `count` placements across a square of chunks and returns how many survive a cull.
    // `microseconds` is the cull time. Used as the 10k-instance check.
    int32_t cullSyntheticPlacements(
        int32_t count, float eyeX, float eyeY, float eyeZ, float maxDistance, int64_t& microseconds);
} // namespace OpenRCT2::View3D
