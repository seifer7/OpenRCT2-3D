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
#include <cstring>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace
{
    float _viewDistanceTiles = OpenRCT2::View3D::kDefaultViewDistanceTiles;
    float _distantSpriteTiles = OpenRCT2::View3D::kDefaultDistantSpriteTiles;

    constexpr float kFootprintMarginTiles = 4.0f;

    float footprintFromSamples(const float clip[16], float focusX, float focusY, bool capFar, float farCap)
    {
        // Bottom corners and the left/right edge through the focus. Top corners are the far side.
        const float samples[6][2] = {
            { -1.0f, -1.0f }, { 1.0f, -1.0f }, { -1.0f, 0.0f }, { 1.0f, 0.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f },
        };
        float farthest = 0.0f;
        int32_t hits = 0;
        for (const float* sample : samples)
        {
            float x = 0.0f;
            float y = 0.0f;
            if (!OpenRCT2::View3D::groundPlanePoint(clip, sample[0], sample[1], x, y))
            {
                continue;
            }
            const float dx = x - focusX;
            const float dy = y - focusY;
            float distance = std::sqrt(dx * dx + dy * dy);
            if (capFar && sample[1] > 0.5f)
            {
                distance = std::min(distance, farCap);
            }
            farthest = std::max(farthest, distance);
            hits++;
        }
        if (hits == 0)
        {
            return OpenRCT2::View3D::sceneryViewDistanceTiles();
        }
        return std::max(OpenRCT2::View3D::sceneryViewDistanceTiles(), farthest + kFootprintMarginTiles);
    }
} // namespace

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

    float sceneryDistantSpriteTiles()
    {
        return _distantSpriteTiles;
    }

    void setSceneryDistantSpriteTiles(float tiles)
    {
        _distantSpriteTiles = tiles > 0.0f ? tiles : kDefaultDistantSpriteTiles;
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

    static float closestOnSegment(float value, float minValue, float maxValue)
    {
        if (value < minValue)
        {
            return minValue;
        }
        if (value > maxValue)
        {
            return maxValue;
        }
        return value;
    }

    bool boxOutsideGroundDistance(const ChunkBox& box, float focusX, float focusY, float maxDistanceTiles)
    {
        const float cx = closestOnSegment(focusX, box.minX, box.maxX);
        const float cy = closestOnSegment(focusY, box.minY, box.maxY);
        const float dx = cx - focusX;
        const float dy = cy - focusY;
        return dx * dx + dy * dy > maxDistanceTiles * maxDistanceTiles;
    }

    bool groundPlanePoint(const float clip[16], float ndcX, float ndcY, float& x, float& y)
    {
        const float a00 = clip[0] - ndcX * clip[3];
        const float a01 = clip[4] - ndcX * clip[7];
        const float a10 = clip[1] - ndcY * clip[3];
        const float a11 = clip[5] - ndcY * clip[7];
        const float det = a00 * a11 - a01 * a10;
        if (std::fabs(det) < 1.0e-6f)
        {
            return false;
        }
        const float b0 = ndcX * clip[15] - clip[12];
        const float b1 = ndcY * clip[15] - clip[13];
        x = (b0 * a11 - a01 * b1) / det;
        y = (a00 * b1 - b0 * a10) / det;
        const float clipW = clip[3] * x + clip[7] * y + clip[15];
        return clipW > 0.0f;
    }

    float groundFootprintRadius(const float clip[16], float eyeX, float eyeY, float focusX, float focusY)
    {
        // Far side capped at the eye-to-focus distance. Uncapped, the top of a zoom-3
        // frame meets the ground hundreds of tiles out and the 3D set explodes.
        return footprintFromSamples(clip, focusX, focusY, true, sceneryCullRadius(eyeX, eyeY, focusX, focusY));
    }

    static bool farGroundMissed(const float clip[16])
    {
        const float samples[3][2] = { { -1.0f, 1.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
        for (const float* sample : samples)
        {
            float x = 0.0f;
            float y = 0.0f;
            if (!groundPlanePoint(clip, sample[0], sample[1], x, y))
            {
                return true;
            }
        }
        return false;
    }

    float groundReachRadius(const float clip[16], float focusX, float focusY)
    {
        float reach = footprintFromSamples(clip, focusX, focusY, false, 0.0f);
        // A fly camera near the horizon misses the ground at the top of the view.
        // The sprite layer still covers out to its far limit instead of leaving that band empty.
        if (farGroundMissed(clip))
        {
            reach = std::max(reach, sceneryDistantSpriteTiles());
        }
        return reach;
    }

    float distantSpriteRadius(const float clip[16], float focusX, float focusY, float farLimitTiles)
    {
        const float reach = groundReachRadius(clip, focusX, focusY);
        const float limit = farLimitTiles > 0.0f ? farLimitTiles : sceneryDistantSpriteTiles();
        return std::min(reach, limit);
    }

    float mapCornerDistance(float focusX, float focusY, float tilesX, float tilesY)
    {
        const float xs[2] = { 0.0f, std::max(tilesX, 0.0f) };
        const float ys[2] = { 0.0f, std::max(tilesY, 0.0f) };
        float farthest = 0.0f;
        for (float x : xs)
        {
            for (float y : ys)
            {
                const float dx = x - focusX;
                const float dy = y - focusY;
                farthest = std::max(farthest, std::sqrt(dx * dx + dy * dy));
            }
        }
        return farthest;
    }

    bool chunkInDistantSprites(
        const ChunkBox& box, float focusX, float focusY, float meshRadius, float spriteRadius, const float planes[6][4])
    {
        if (!boxOutsideGroundDistance(box, focusX, focusY, meshRadius))
        {
            return false;
        }
        if (boxOutsideGroundDistance(box, focusX, focusY, spriteRadius))
        {
            return false;
        }
        return !boxOutsideFrustum(box, planes);
    }

    static uint64_t imageBits(const ImageId& image)
    {
        static_assert(std::is_trivially_copyable_v<ImageId>);
        static_assert(sizeof(ImageId) == 8);
        uint64_t bits = 0;
        std::memcpy(&bits, &image, sizeof(bits));
        return bits;
    }

    void buildChunkSpriteRuns(SceneryChunk& chunk)
    {
        chunk.spriteInstances.clear();
        chunk.spriteInstances.reserve(chunk.placements.size());
        for (const ChunkPlacement& placement : chunk.placements)
        {
            chunk.spriteInstances.push_back(ChunkSpritePoint{ placement.x, placement.y, placement.z });
        }
        for (int face = 0; face < 4; face++)
        {
            std::vector<uint32_t>& order = chunk.spriteOrder[face];
            std::vector<ChunkSpriteRun>& runs = chunk.spriteRuns[face];
            order.clear();
            runs.clear();
            std::vector<std::pair<uint64_t, uint32_t>> keys;
            keys.reserve(chunk.placements.size());
            for (uint32_t i = 0; i < static_cast<uint32_t>(chunk.placements.size()); i++)
            {
                if (!chunk.placements[i].spriteFace[face])
                {
                    continue;
                }
                keys.emplace_back(imageBits(chunk.placements[i].spriteImage[face]), i);
            }
            std::sort(
                keys.begin(), keys.end(), [](const std::pair<uint64_t, uint32_t>& a, const std::pair<uint64_t, uint32_t>& b) {
                    if (a.first != b.first)
                    {
                        return a.first < b.first;
                    }
                    return a.second < b.second;
                });
            for (const std::pair<uint64_t, uint32_t>& item : keys)
            {
                order.push_back(item.second);
                const uint32_t slot = static_cast<uint32_t>(order.size() - 1);
                if (runs.empty() || runs.back().key != item.first)
                {
                    ChunkSpriteRun run;
                    run.key = item.first;
                    run.image = chunk.placements[item.second].spriteImage[face];
                    run.begin = slot;
                    run.count = 1;
                    run.vegetation = chunk.placements[item.second].vegetation;
                    runs.push_back(run);
                }
                else
                {
                    runs.back().count++;
                    if (!chunk.placements[item.second].vegetation)
                    {
                        runs.back().vegetation = false;
                    }
                }
            }
        }
    }

    float sceneryCullRadius(float eyeX, float eyeY, float focusX, float focusY)
    {
        const float dx = eyeX - focusX;
        const float dy = eyeY - focusY;
        return std::max(sceneryViewDistanceTiles(), std::sqrt(dx * dx + dy * dy));
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
