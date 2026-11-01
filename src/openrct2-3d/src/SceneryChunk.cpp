/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "SceneryChunk.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
    float _viewDistanceTiles = OpenRCT2::View3D::kDefaultViewDistanceTiles;
}

namespace OpenRCT2::View3D
{
    float sceneryViewDistanceTiles()
    {
        return _viewDistanceTiles;
    }

    void setSceneryViewDistanceTiles(float tiles)
    {
        _viewDistanceTiles = tiles > 0.0f ? tiles : kDefaultViewDistanceTiles;
    }

    void SceneryChunkGrid::reset(int32_t tilesX, int32_t tilesY)
    {
        _tilesX = std::max(tilesX, 0);
        _tilesY = std::max(tilesY, 0);
        _chunksX = (_tilesX + kSceneryChunkTiles - 1) / kSceneryChunkTiles;
        _chunksY = (_tilesY + kSceneryChunkTiles - 1) / kSceneryChunkTiles;
        _chunks.assign(static_cast<size_t>(_chunksX) * static_cast<size_t>(_chunksY), SceneryChunk{});
        markAllDirty();
    }

    void SceneryChunkGrid::markAllDirty()
    {
        for (SceneryChunk& chunk : _chunks)
        {
            chunk.dirty = true;
        }
    }

    int32_t SceneryChunkGrid::index(int32_t chunkX, int32_t chunkY) const
    {
        return chunkY * _chunksX + chunkX;
    }

    void SceneryChunkGrid::markTile(int32_t tileX, int32_t tileY)
    {
        if (tileX < 0 || tileY < 0 || tileX >= _tilesX || tileY >= _tilesY)
        {
            return;
        }
        const int32_t chunkX = tileX / kSceneryChunkTiles;
        const int32_t chunkY = tileY / kSceneryChunkTiles;
        _chunks[static_cast<size_t>(index(chunkX, chunkY))].dirty = true;
    }

    bool SceneryChunkGrid::dirty(int32_t chunkX, int32_t chunkY) const
    {
        const SceneryChunk* chunk = at(chunkX, chunkY);
        return chunk != nullptr && chunk->dirty;
    }

    SceneryChunk* SceneryChunkGrid::at(int32_t chunkX, int32_t chunkY)
    {
        if (chunkX < 0 || chunkY < 0 || chunkX >= _chunksX || chunkY >= _chunksY)
        {
            return nullptr;
        }
        return &_chunks[static_cast<size_t>(index(chunkX, chunkY))];
    }

    const SceneryChunk* SceneryChunkGrid::at(int32_t chunkX, int32_t chunkY) const
    {
        if (chunkX < 0 || chunkY < 0 || chunkX >= _chunksX || chunkY >= _chunksY)
        {
            return nullptr;
        }
        return &_chunks[static_cast<size_t>(index(chunkX, chunkY))];
    }

    int32_t SceneryChunkGrid::dirtyCount() const
    {
        int32_t count = 0;
        for (const SceneryChunk& chunk : _chunks)
        {
            if (chunk.dirty)
            {
                count++;
            }
        }
        return count;
    }

    void extractFrustumPlanes(const float clip[16], float planes[6][4])
    {
        // Column-major clip matrix. Each plane is ax+by+cz+d, then normalised.
        const auto plane = [&](int32_t index, float a, float b, float c, float d) {
            const float length = std::sqrt(a * a + b * b + c * c);
            const float scale = length > 1.0e-8f ? 1.0f / length : 0.0f;
            planes[index][0] = a * scale;
            planes[index][1] = b * scale;
            planes[index][2] = c * scale;
            planes[index][3] = d * scale;
        };
        plane(0, clip[3] + clip[0], clip[7] + clip[4], clip[11] + clip[8], clip[15] + clip[12]);
        plane(1, clip[3] - clip[0], clip[7] - clip[4], clip[11] - clip[8], clip[15] - clip[12]);
        plane(2, clip[3] + clip[1], clip[7] + clip[5], clip[11] + clip[9], clip[15] + clip[13]);
        plane(3, clip[3] - clip[1], clip[7] - clip[5], clip[11] - clip[9], clip[15] - clip[13]);
        plane(4, clip[3] + clip[2], clip[7] + clip[6], clip[11] + clip[10], clip[15] + clip[14]);
        plane(5, clip[3] - clip[2], clip[7] - clip[6], clip[11] - clip[10], clip[15] - clip[14]);
    }

    bool boxOutsideFrustum(const ChunkBox& box, const float planes[6][4])
    {
        for (int32_t plane = 0; plane < 6; plane++)
        {
            const float px = planes[plane][0] >= 0.0f ? box.maxX : box.minX;
            const float py = planes[plane][1] >= 0.0f ? box.maxY : box.minY;
            const float pz = planes[plane][2] >= 0.0f ? box.maxZ : box.minZ;
            if (planes[plane][0] * px + planes[plane][1] * py + planes[plane][2] * pz + planes[plane][3] < 0.0f)
            {
                return true;
            }
        }
        return false;
    }

    bool boxOutsideDistance(const ChunkBox& box, float eyeX, float eyeY, float eyeZ, float maxDistanceTiles)
    {
        const float cx = (box.minX + box.maxX) * 0.5f;
        const float cy = (box.minY + box.maxY) * 0.5f;
        const float cz = (box.minZ + box.maxZ) * 0.5f;
        const float dx = cx - eyeX;
        const float dy = cy - eyeY;
        const float dz = cz - eyeZ;
        return dx * dx + dy * dy + dz * dz > maxDistanceTiles * maxDistanceTiles;
    }

    bool boxOutsideGroundDistance(const ChunkBox& box, float focusX, float focusY, float maxDistanceTiles)
    {
        const float cx = (box.minX + box.maxX) * 0.5f;
        const float cy = (box.minY + box.maxY) * 0.5f;
        const float dx = cx - focusX;
        const float dy = cy - focusY;
        return dx * dx + dy * dy > maxDistanceTiles * maxDistanceTiles;
    }

    float sceneryCullRadius(float eyeX, float eyeY, float focusX, float focusY)
    {
        const float dx = eyeX - focusX;
        const float dy = eyeY - focusY;
        const float zoomDistance = std::sqrt(dx * dx + dy * dy);
        return std::max(sceneryViewDistanceTiles(), zoomDistance);
    }

    int32_t cullSyntheticPlacements(int32_t count, float eyeX, float eyeY, float eyeZ, float maxDistance, int64_t& microseconds)
    {
        const int32_t side = std::max(
            kSceneryChunkTiles, static_cast<int32_t>(std::sqrt(static_cast<float>(std::max(count, 1)))) + 1);
        SceneryChunkGrid grid;
        grid.reset(side, side);
        int32_t left = count;
        for (int32_t cy = 0; cy < grid.chunksY() && left > 0; cy++)
        {
            for (int32_t cx = 0; cx < grid.chunksX() && left > 0; cx++)
            {
                SceneryChunk* chunk = grid.at(cx, cy);
                chunk->dirty = false;
                chunk->built = true;
                chunk->box.minX = static_cast<float>(cx * kSceneryChunkTiles);
                chunk->box.minY = static_cast<float>(cy * kSceneryChunkTiles);
                chunk->box.minZ = 0.0f;
                chunk->box.maxX = chunk->box.minX + static_cast<float>(kSceneryChunkTiles);
                chunk->box.maxY = chunk->box.minY + static_cast<float>(kSceneryChunkTiles);
                chunk->box.maxZ = 4.0f;
                const int32_t here = std::min(left, 64);
                chunk->placements.resize(static_cast<size_t>(here));
                left -= here;
            }
        }
        float planes[6][4] = {};
        // Identity clip keeps everything in the positive half-spaces we set below to accept all.
        float clip[16] = {};
        clip[0] = clip[5] = clip[10] = clip[15] = 1.0f;
        extractFrustumPlanes(clip, planes);
        const auto start = std::chrono::steady_clock::now();
        int32_t visible = 0;
        for (int32_t cy = 0; cy < grid.chunksY(); cy++)
        {
            for (int32_t cx = 0; cx < grid.chunksX(); cx++)
            {
                const SceneryChunk* chunk = grid.at(cx, cy);
                if (chunk == nullptr || !chunk->built)
                {
                    continue;
                }
                if (boxOutsideDistance(chunk->box, eyeX, eyeY, eyeZ, maxDistance))
                {
                    continue;
                }
                if (boxOutsideFrustum(chunk->box, planes))
                {
                    continue;
                }
                visible += static_cast<int32_t>(chunk->placements.size());
            }
        }
        microseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        return visible;
    }
} // namespace OpenRCT2::View3D
