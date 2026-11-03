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
#include <openrct2/drawing/ImageId.hpp>
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
        // Sprite images are stored for the fallback. They are uploaded only when
        // this placement is drawn without a model.
        ImageId spriteImage[4] = {};
        bool spriteFace[4] = {};
        // Top of the object's declared height, in viewer units. entry height, not a fixed box.
        float topZ = 0.0f;
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

    // One scenery anchor. Distant draws copy these; they are not rebuilt per frame.
    struct ChunkSpritePoint
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    // A run of the same sprite image inside one direction face. `begin` indexes
    // `spriteOrder[face]`, which indexes `spriteInstances`.
    struct ChunkSpriteRun
    {
        ImageId image;
        uint64_t key = 0;
        uint32_t begin = 0;
        uint32_t count = 0;
        bool vegetation = false;
    };

    struct SceneryChunk
    {
        bool dirty = true;
        bool built = false;
        ChunkBox box;
        std::vector<ChunkPlacement> placements;
        // Filled with the placements when the chunk is rebuilt. Four faces, one
        // chosen at draw from the camera. The instance positions are shared.
        std::vector<ChunkSpritePoint> spriteInstances;
        std::vector<uint32_t> spriteOrder[4];
        std::vector<ChunkSpriteRun> spriteRuns[4];
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
    // Horizontal distance from the view focus. The eye's height is not part of it.
    bool boxOutsideGroundDistance(const ChunkBox& box, float focusX, float focusY, float maxDistanceTiles);
    // Where an NDC point on the ground plane (z = 0) lands. False when the ray misses
    // the ground in front of the camera.
    bool groundPlanePoint(const float clip[16], float ndcX, float ndcY, float& x, float& y);
    // Near screen corners and the width at the focus, on z = 0. The top corners are
    // capped at the eye-to-focus distance: they meet the ground a few degrees under the horizon.
    // That cap is the 3D radius. Do not drop it: an uncapped top corner is the horizon.
    float groundFootprintRadius(const float clip[16], float eyeX, float eyeY, float focusX, float focusY);
    // The same samples with the far side left uncapped. The sprite layer starts from this.
    float groundReachRadius(const float clip[16], float focusX, float focusY);
    // min(ground reach, farLimit). A non-positive limit uses sceneryDistantSpriteTiles().
    float distantSpriteRadius(const float clip[16], float focusX, float focusY, float farLimitTiles);
    // Distance from the focus to the farthest corner of the map rectangle.
    float mapCornerDistance(float focusX, float focusY, float tilesX, float tilesY);
    float sceneryCullRadius(float eyeX, float eyeY, float focusX, float focusY);
    // Outside the 3D radius, inside the sprite radius, and inside the frustum.
    bool chunkInDistantSprites(
        const ChunkBox& box, float focusX, float focusY, float meshRadius, float spriteRadius, const float planes[6][4]);
    // Group placements into per-face sprite runs. Called when a chunk is rebuilt.
    void buildChunkSpriteRuns(SceneryChunk& chunk);

    // The walk `appendDistant` uses. `fn` is called once per sprite run on a distant chunk.
    template<typename Fn>
    void appendDistantPlacements(
        const SceneryChunkGrid& grid, const float planes[6][4], float focusX, float focusY, float meshRadius,
        float spriteRadius, int face, Fn&& fn)
    {
        const int indexFace = face & 3;
        for (int32_t y = 0; y < grid.chunksY(); y++)
        {
            for (int32_t x = 0; x < grid.chunksX(); x++)
            {
                const SceneryChunk* chunk = grid.at(x, y);
                if (chunk == nullptr || !chunk->built)
                {
                    continue;
                }
                if (!chunkInDistantSprites(chunk->box, focusX, focusY, meshRadius, spriteRadius, planes))
                {
                    continue;
                }
                for (const ChunkSpriteRun& run : chunk->spriteRuns[indexFace])
                {
                    if (run.count == 0)
                    {
                        continue;
                    }
                    fn(*chunk, run);
                }
            }
        }
    }

    // Builds `count` placements across a square of chunks and returns how many survive a cull.
    // `microseconds` is the cull time. Used as the 10k-instance check.
    int32_t cullSyntheticPlacements(
        int32_t count, float eyeX, float eyeY, float eyeZ, float maxDistance, int64_t& microseconds);
} // namespace OpenRCT2::View3D
