/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "TestData.h"
#include "openrct2-3d/src/GlbMesh.hpp"
#include "openrct2-3d/src/GpuBudget.hpp"
#include "openrct2-3d/src/MeshDraw.hpp"
#include "openrct2-3d/src/ModelCache.hpp"
#include "openrct2-3d/src/ModelLoadQueue.hpp"
#include "openrct2-3d/src/ParkView.hpp"
#include "openrct2-3d/src/SceneryChunk.hpp"
#include "openrct2-3d/src/SceneryDraw.hpp"
#include "openrct2-3d/src/SceneryScale.hpp"
#include "openrct2-3d/src/SpriteQuad.hpp"
#include "openrct2-3d/src/ViewerScale.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <gtest/gtest.h>
#include <openrct2/Context.h>
#include <openrct2/Game.h>
#include <openrct2/GameState.h>
#include <openrct2/OpenRCT2.h>
#include <openrct2/actions/GameActionRunner.h>
#include <openrct2/actions/scenery/SmallSceneryPlaceAction.h>
#include <openrct2/core/File.h>
#include <openrct2/core/Path.hpp>
#include <openrct2/core/String.hpp>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/drawing/FilterPaletteIds.h>
#include <openrct2/object/Object.h>
#include <openrct2/object/ObjectManager.h>
#include <openrct2/world/Location.hpp>
#include <openrct2/world/Map.h>
#include <openrct2/world/Scenery.h>
#include <openrct2/world/TileElementsView.h>
#include <openrct2/world/tile_element/SmallSceneryElement.h>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #undef CreateDirectory
    #undef DeleteDirectory
#endif

using namespace OpenRCT2::View3D;

namespace
{
    int32_t gHookX = -1;
    int32_t gHookY = -1;

    void recordMapChange(int32_t tileX, int32_t tileY)
    {
        gHookX = tileX;
        gHookY = tileY;
    }
} // namespace

TEST(SceneryScale, MapChangedHookReceivesTheTile)
{
    gHookX = -1;
    gHookY = -1;
    OpenRCT2::SetMapChangedHook(&recordMapChange);
    OpenRCT2::NotifyMapChanged(3, 4);
    EXPECT_EQ(gHookX, 3);
    EXPECT_EQ(gHookY, 4);
    OpenRCT2::SetMapChangedHook(nullptr);
}

TEST(SceneryScale, QuarterTileAnchorMatchesPaint)
{
    // The game table is the quadrant centre. Paint subtracts 1 and, at rotation 0,
    // does not add a view rotation. vOffsetCentre is not part of this branch.
    const int32_t paintedX[4] = { 7, 7, 23, 23 };
    const int32_t paintedY[4] = { 7, 23, 23, 7 };
    for (uint8_t quadrant = 0; quadrant < 4; quadrant++)
    {
        EXPECT_EQ(SceneryQuadrantOffsets[quadrant].x - 1, paintedX[quadrant]);
        EXPECT_EQ(SceneryQuadrantOffsets[quadrant].y - 1, paintedY[quadrant]);
        EXPECT_EQ(kQuarterTileSimX[quadrant], paintedX[quadrant]);
        EXPECT_EQ(kQuarterTileSimY[quadrant], paintedY[quadrant]);

        float x = 0.0f;
        float y = 0.0f;
        sceneryAnchor(4, 9, false, quadrant, x, y);
        EXPECT_FLOAT_EQ(x, 4.0f + static_cast<float>(paintedX[quadrant]) / 32.0f);
        EXPECT_FLOAT_EQ(y, 9.0f + static_cast<float>(paintedY[quadrant]) / 32.0f);
        EXPECT_NE(x, 4.5f);
        EXPECT_NE(y, 9.5f);

        // Stored quadrant 0..3 only. A value with high bits uses the low two.
        float wrappedX = 0.0f;
        float wrappedY = 0.0f;
        sceneryAnchor(4, 9, false, static_cast<uint8_t>(quadrant + 4), wrappedX, wrappedY);
        EXPECT_FLOAT_EQ(wrappedX, x);
        EXPECT_FLOAT_EQ(wrappedY, y);
    }

    float fullX = 0.0f;
    float fullY = 0.0f;
    sceneryAnchor(4, 9, true, 2, fullX, fullY);
    EXPECT_FLOAT_EQ(fullX, 4.5f);
    EXPECT_FLOAT_EQ(fullY, 9.5f);
    float otherX = 0.0f;
    float otherY = 0.0f;
    sceneryAnchor(4, 9, true, 0, otherX, otherY);
    EXPECT_FLOAT_EQ(otherX, fullX);
    EXPECT_FLOAT_EQ(otherY, fullY);
}

TEST(SceneryScale, QuarterTileYawPivotsOnQuadrant)
{
    float anchorX = 0.0f;
    float anchorY = 0.0f;
    sceneryAnchor(0, 0, false, 1, anchorX, anchorY);
    const float localX = 0.2f;
    const float localY = -0.1f;
    float rotatedX = 0.0f;
    float rotatedY = 0.0f;
    rotateYaw(modelYawRadians(1), localX, localY, rotatedX, rotatedY);

    // The instance shader is world = R * local + anchor. The local origin stays put.
    float zeroX = 1.0f;
    float zeroY = 1.0f;
    rotateYaw(modelYawRadians(2), 0.0f, 0.0f, zeroX, zeroY);
    EXPECT_FLOAT_EQ(anchorX + zeroX, anchorX);
    EXPECT_FLOAT_EQ(anchorY + zeroY, anchorY);

    const float worldX = anchorX + rotatedX;
    const float worldY = anchorY + rotatedY;
    const float centreX = 0.5f + rotatedX;
    const float centreY = 0.5f + rotatedY;
    EXPECT_NE(worldX, centreX);
    EXPECT_NE(worldY, centreY);

    const float before = localX * localX + localY * localY;
    const float after = rotatedX * rotatedX + rotatedY * rotatedY;
    EXPECT_NEAR(before, after, 1.0e-6f);

    // Direction 1 is a clockwise quarter turn: (x, y) -> (y, -x).
    EXPECT_NEAR(rotatedX, localY, 1.0e-5f);
    EXPECT_NEAR(rotatedY, -localX, 1.0e-5f);
    EXPECT_NEAR(std::hypot(worldX - anchorX, worldY - anchorY), std::hypot(localX, localY), 1.0e-5f);
}

TEST(SceneryScale, DirtyTrackingIsPerChunk)
{
    SceneryChunkGrid grid;
    grid.reset(64, 48);
    EXPECT_EQ(grid.chunksX(), 4);
    EXPECT_EQ(grid.chunksY(), 3);
    EXPECT_EQ(grid.dirtyCount(), 12);

    for (int32_t y = 0; y < grid.chunksY(); y++)
    {
        for (int32_t x = 0; x < grid.chunksX(); x++)
        {
            grid.at(x, y)->dirty = false;
        }
    }
    EXPECT_EQ(grid.dirtyCount(), 0);

    grid.markTile(1, 1);
    grid.markTile(15, 15);
    EXPECT_EQ(grid.dirtyCount(), 1);
    EXPECT_TRUE(grid.dirty(0, 0));

    grid.markTile(16, 0);
    EXPECT_EQ(grid.dirtyCount(), 2);
    EXPECT_TRUE(grid.dirty(1, 0));

    grid.markTile(-1, 0);
    grid.markTile(1000, 1000);
    EXPECT_EQ(grid.dirtyCount(), 2);

    grid.markAllDirty();
    EXPECT_EQ(grid.dirtyCount(), 12);
}

TEST(SceneryScale, DistanceAndFrustumRejectFarBoxes)
{
    ChunkBox nearBox;
    nearBox.maxX = 16.0f;
    nearBox.maxY = 16.0f;
    nearBox.maxZ = 4.0f;
    EXPECT_FALSE(boxOutsideDistance(nearBox, 8.0f, 8.0f, 2.0f, kDefaultViewDistanceTiles));

    ChunkBox farBox = nearBox;
    farBox.minX = 200.0f;
    farBox.maxX = 216.0f;
    EXPECT_TRUE(boxOutsideDistance(farBox, 0.0f, 0.0f, 0.0f, kDefaultViewDistanceTiles));

    float clip[16] = {};
    clip[0] = clip[5] = clip[10] = clip[15] = 1.0f;
    float planes[6][4] = {};
    extractFrustumPlanes(clip, planes);
    EXPECT_FALSE(boxOutsideFrustum(nearBox, planes));

    ChunkBox behind = nearBox;
    behind.minX = -30.0f;
    behind.maxX = -20.0f;
    EXPECT_TRUE(boxOutsideFrustum(behind, planes));
}

TEST(SceneryScale, TenThousandPlacementsCull)
{
    int64_t microseconds = -1;
    const int32_t visible = cullSyntheticPlacements(10000, 0.0f, 0.0f, 0.0f, kDefaultViewDistanceTiles, microseconds);
    EXPECT_GT(visible, 0);
    EXPECT_LT(visible, 10000);
    EXPECT_GE(microseconds, 0);
}

TEST(SceneryScale, AllPinnedBudgetRefusesTheNewcomer)
{
    LruBudget budget(100);
    budget.insert(1, 60, 1);
    budget.insert(2, 40, 2);
    const std::unordered_set<uint32_t> pinned{ 1u, 2u };
    EXPECT_TRUE(budget.evict(10, pinned).empty());
    EXPECT_EQ(budget.used(), 100u);
    EXPECT_FALSE(budget.evictOne(pinned).has_value());
}

TEST(SceneryScale, FreedAtlasRectsCoalesce)
{
    AtlasPacker packer(16, 1, 0);
    AtlasSlot top;
    AtlasSlot bottom;
    ASSERT_TRUE(packer.pack(16, 8, top));
    ASSERT_TRUE(packer.pack(16, 8, bottom));
    packer.free(top);
    packer.free(bottom);
    AtlasSlot whole;
    ASSERT_TRUE(packer.pack(16, 16, whole));
}

TEST(SceneryScale, LruDropsColdestUnpinned)
{
    LruBudget budget(100);
    budget.insert(1, 40, 1);
    budget.insert(2, 40, 2);
    budget.insert(3, 40, 3);
    EXPECT_EQ(budget.used(), 120u);

    const std::vector<uint32_t> dropped = budget.evict(40, { 3 });
    ASSERT_EQ(dropped.size(), 2u);
    EXPECT_EQ(dropped[0], 1u);
    EXPECT_EQ(dropped[1], 2u);
    EXPECT_TRUE(budget.contains(3));
    EXPECT_FALSE(budget.contains(1));

    const std::vector<uint32_t> kept = budget.evict(1000, { 3 });
    EXPECT_TRUE(kept.empty());
    EXPECT_TRUE(budget.contains(3));
}

TEST(SceneryScale, GeometrySlabsShareHandles)
{
    GeometryArena arena(64, 256, 2);
    BufferRange vertices;
    BufferRange indices;
    ASSERT_TRUE(arena.alloc(10, 30, vertices, indices));
    EXPECT_EQ(arena.slabs(), 1u);
    EXPECT_EQ(arena.geometryHandles(), 2u);
    EXPECT_LT(arena.geometryHandles(), kHandleCap);

    BufferRange vertices2;
    BufferRange indices2;
    ASSERT_TRUE(arena.alloc(10, 30, vertices2, indices2));
    EXPECT_EQ(arena.slabs(), 1u);
    EXPECT_EQ(arena.geometryHandles(), 2u);
    EXPECT_NE(vertices.offset, vertices2.offset);

    arena.free(vertices, indices);
    arena.free(vertices2, indices2);

    EXPECT_FALSE(arena.alloc(65, 10, vertices, indices));
    ASSERT_TRUE(arena.alloc(64, 256, vertices, indices));
    ASSERT_TRUE(arena.alloc(64, 256, vertices2, indices2));
    EXPECT_EQ(arena.slabs(), 2u);
    EXPECT_EQ(arena.geometryHandles(), 4u);
    BufferRange overflowV;
    BufferRange overflowI;
    EXPECT_FALSE(arena.alloc(64, 256, overflowV, overflowI));
}

TEST(SceneryScale, AtlasPacksOntoSharedPages)
{
    AtlasPacker packer(64, 2);
    AtlasSlot first;
    AtlasSlot second;
    ASSERT_TRUE(packer.pack(32, 32, first));
    ASSERT_TRUE(packer.pack(32, 32, second));
    EXPECT_EQ(packer.pageCount(), 1u);
    EXPECT_EQ(first.page, second.page);
    EXPECT_NE(first.x, second.x);

    AtlasSlot third;
    ASSERT_TRUE(packer.pack(64, 64, third));
    EXPECT_EQ(packer.pageCount(), 2u);
    EXPECT_FALSE(packer.pack(64, 64, third));
    EXPECT_EQ(packer.pageCount(), 2u);
}

TEST(SceneryScale, LoaderStateMachine)
{
    ModelLoadQueue queue(
        [](const std::string& identifier, std::string& error) -> std::optional<CpuMesh> {
            if (identifier == "ok")
            {
                CpuMesh mesh;
                MeshPrimitive primitive;
                primitive.vertices.push_back(MeshVertex{});
                primitive.indices.push_back(0);
                mesh.primitives.push_back(std::move(primitive));
                return mesh;
            }
            error = "missing";
            return std::nullopt;
        },
        false);

    EXPECT_EQ(queue.phase("ok"), ModelLoadPhase::absent);
    queue.request("ok");
    EXPECT_EQ(queue.phase("ok"), ModelLoadPhase::queued);
    queue.pump();
    EXPECT_EQ(queue.phase("ok"), ModelLoadPhase::ready);
    std::string taken;
    const std::optional<CpuMesh> mesh = queue.takeReady(taken);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(taken, "ok");
    EXPECT_EQ(queue.phase("ok"), ModelLoadPhase::delivered);
    EXPECT_FALSE(queue.takeReady(taken).has_value());

    queue.request("missing");
    queue.pump();
    EXPECT_EQ(queue.phase("missing"), ModelLoadPhase::failed);
    queue.forget("missing");
    EXPECT_EQ(queue.phase("missing"), ModelLoadPhase::absent);

    queue.request("bad");
    queue.pump();
    EXPECT_EQ(queue.phase("bad"), ModelLoadPhase::failed);
    queue.requestAbsent({ "bad" });
    EXPECT_EQ(queue.phase("bad"), ModelLoadPhase::failed);
    std::string failed;
    EXPECT_FALSE(queue.takeReady(failed).has_value());
}

TEST(SceneryScale, AtlasSlotIsReusedAfterFree)
{
    AtlasPacker packer(32, 1, 0);
    AtlasSlot first;
    ASSERT_TRUE(packer.pack(32, 32, first));
    AtlasSlot blocked;
    EXPECT_FALSE(packer.pack(8, 8, blocked));
    packer.free(first);
    AtlasSlot again;
    ASSERT_TRUE(packer.pack(32, 32, again));
    EXPECT_EQ(packer.pageCount(), 1u);
    EXPECT_EQ(again.x, first.x);
    EXPECT_EQ(again.y, first.y);
}

TEST(SceneryScale, AtlasGutterStaysOutsideTheImage)
{
    AtlasPacker packer(32, 1, 2);
    AtlasSlot slot;
    ASSERT_TRUE(packer.pack(28, 28, slot));
    EXPECT_EQ(slot.x, 2u);
    EXPECT_EQ(slot.y, 2u);
    EXPECT_EQ(slot.width, 28u);
    EXPECT_FALSE(packer.pack(8, 8, slot));
    packer.free(AtlasSlot{ 0, 2, 2, 28, 28 });
    ASSERT_TRUE(packer.pack(28, 28, slot));
    EXPECT_EQ(slot.x, 2u);
}

TEST(SceneryScale, SlabIndexAppliesTheBaseOnce)
{
    EXPECT_EQ(slabVertexIndex(0, 4), 4u);
    EXPECT_EQ(slabVertexIndex(100, 4), 104u);
    EXPECT_NE(slabVertexIndex(100, 4), 204u);
}

TEST(SceneryScale, LruTouchMovesAnIdToMostRecent)
{
    LruBudget budget(100);
    budget.insert(1, 40, 1);
    budget.insert(2, 40, 2);
    budget.touch(1, 9);
    const std::vector<uint32_t> dropped = budget.evict(40, {});
    ASSERT_EQ(dropped.size(), 1u);
    EXPECT_EQ(dropped[0], 2u);
    EXPECT_TRUE(budget.contains(1));
    EXPECT_FALSE(budget.contains(2));
}

TEST(SceneryScale, ViewDistanceIsConfigurable)
{
    const float saved = sceneryViewDistanceTiles();
    setSceneryViewDistanceTiles(12.0f);
    EXPECT_FLOAT_EQ(sceneryViewDistanceTiles(), 12.0f);
    setSceneryViewDistanceTiles(0.0f);
    EXPECT_FLOAT_EQ(sceneryViewDistanceTiles(), kDefaultViewDistanceTiles);
    setSceneryViewDistanceTiles(saved);
}

TEST(SceneryScale, FootprintKeepsCornerAndEdge)
{
    // ndc = world / 136, so the four screen corners sit 136 tiles from the origin on each axis.
    float clip[16] = {};
    clip[0] = 1.0f;
    clip[5] = 1.0f;
    clip[15] = 136.0f;
    const float radius = groundFootprintRadius(clip, 0.0f, -1000.0f, 0.0f, 0.0f);
    EXPECT_GT(radius, 136.0f * std::sqrt(2.0f));

    ChunkBox centre;
    centre.maxX = 16.0f;
    centre.maxY = 16.0f;
    centre.maxZ = 4.0f;
    EXPECT_FALSE(boxOutsideGroundDistance(centre, 0.0f, 0.0f, radius));

    ChunkBox edge;
    edge.minX = 120.0f;
    edge.maxX = 136.0f;
    edge.minY = -8.0f;
    edge.maxY = 8.0f;
    edge.maxZ = 4.0f;
    EXPECT_FALSE(boxOutsideGroundDistance(edge, 0.0f, 0.0f, radius));

    ChunkBox corner;
    corner.minX = 120.0f;
    corner.maxX = 136.0f;
    corner.minY = 120.0f;
    corner.maxY = 136.0f;
    corner.maxZ = 4.0f;
    EXPECT_FALSE(boxOutsideGroundDistance(corner, 0.0f, 0.0f, radius));

    ChunkBox beyond;
    beyond.minX = 400.0f;
    beyond.maxX = 416.0f;
    beyond.minY = 400.0f;
    beyond.maxY = 416.0f;
    EXPECT_TRUE(boxOutsideGroundDistance(beyond, 0.0f, 0.0f, radius));
}

namespace
{
    struct IsoPose
    {
        float clip[16] = {};
        float eyeX = 0.0f;
        float eyeY = 0.0f;
        float eyeZ = 0.0f;
        float focusX = 0.0f;
        float focusY = 0.0f;
        float planes[6][4] = {};
    };

    void mulClip(float out[16], const float proj[16], const float view[16])
    {
        for (int32_t col = 0; col < 4; col++)
        {
            for (int32_t row = 0; row < 4; row++)
            {
                float sum = 0.0f;
                for (int32_t k = 0; k < 4; k++)
                {
                    sum += proj[row + k * 4] * view[k + col * 4];
                }
                out[row + col * 4] = sum;
            }
        }
    }

    // Hand-built pose for the radius geometry tests. The in-game clip, including
    // bx::mtxMul's argument order, is Park::sceneryClipForFrame.
    IsoPose makeIsoPose(float frameHeight, float tilePixels, float aspect, float focusX, float focusY)
    {
        const float cotHalf = 1.0f / std::tan(50.0f * 0.5f * 3.14159265f / 180.0f);
        const float distance = cotHalf * 1.41421356f * frameHeight / (2.0f * tilePixels) * 0.8f;
        const float pitch = 0.52359878f;
        const float eyeSouth = -distance * std::cos(pitch);
        const float eyeZ = distance * std::sin(pitch);
        const float eyeX = focusX;
        const float eyeY = focusY + eyeSouth;
        const float fx = 0.0f;
        const float fy = -eyeSouth;
        const float fz = -eyeZ;
        const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
        const float fwx = fx / fl;
        const float fwy = fy / fl;
        const float fwz = fz / fl;
        const float rx = fwy;
        const float ry = -fwx;
        const float rl = std::sqrt(rx * rx + ry * ry);
        const float rnx = rx / rl;
        const float rny = ry / rl;
        const float ux = rny * fwz;
        const float uy = -rnx * fwz;
        const float uz = rnx * fwy - rny * fwx;
        float view[16] = {};
        view[0] = rnx;
        view[4] = rny;
        view[12] = -(rnx * eyeX + rny * eyeY);
        view[1] = ux;
        view[5] = uy;
        view[9] = uz;
        view[13] = -(ux * eyeX + uy * eyeY + uz * eyeZ);
        view[2] = -fwx;
        view[6] = -fwy;
        view[10] = -fwz;
        view[14] = fwx * eyeX + fwy * eyeY + fwz * eyeZ;
        view[15] = 1.0f;
        const float f = cotHalf;
        const float n = 0.2f;
        const float farPlane = 8000.0f;
        float proj[16] = {};
        proj[0] = f / aspect;
        proj[5] = f;
        proj[10] = (farPlane + n) / (n - farPlane);
        proj[11] = -1.0f;
        proj[14] = (2.0f * farPlane * n) / (n - farPlane);
        IsoPose pose;
        mulClip(pose.clip, proj, view);
        pose.eyeX = eyeX;
        pose.eyeY = eyeY;
        pose.eyeZ = eyeZ;
        pose.focusX = focusX;
        pose.focusY = focusY;
        extractFrustumPlanes(pose.clip, pose.planes);
        return pose;
    }

    float zoomRadius(float frameHeight, float tilePixels, float aspect)
    {
        float clip[16];
        float eyeX = 0.0f;
        float eyeY = 0.0f;
        float eyeZ = 0.0f;
        float focusX = 0.0f;
        float focusY = 0.0f;
        Park::sceneryClipForFrame(frameHeight, tilePixels, aspect, clip, eyeX, eyeY, eyeZ, focusX, focusY);
        return groundFootprintRadius(clip, eyeX, eyeY, focusX, focusY);
    }

    ChunkBox boxAround(float x, float y)
    {
        ChunkBox box;
        box.minX = x - 1.0f;
        box.maxX = x + 1.0f;
        box.minY = y - 1.0f;
        box.maxY = y + 1.0f;
        box.minZ = 0.0f;
        box.maxZ = 4.0f;
        return box;
    }
} // namespace

TEST(SceneryScale, ZoomedOutRadiusStaysBounded)
{
    const float aspect = 16.0f / 9.0f;
    const float hdZoom2 = zoomRadius(1080.0f, 16.0f, aspect);
    const float hdZoom3 = zoomRadius(1080.0f, 8.0f, aspect);
    const float uhdZoom3 = zoomRadius(2160.0f, 8.0f, aspect);
    // N-B4 was the uncapped horizon. These are the capped radii from the real clip.
    EXPECT_GT(hdZoom2, 40.0f);
    EXPECT_LT(hdZoom2, 160.0f);
    EXPECT_GT(hdZoom3, 80.0f);
    EXPECT_LT(hdZoom3, 260.0f);
    EXPECT_GT(uhdZoom3, 160.0f);
    EXPECT_LT(uhdZoom3, 500.0f);

    float clip[16];
    float eyeX = 0.0f;
    float eyeY = 0.0f;
    float eyeZ = 0.0f;
    float focusX = 0.0f;
    float focusY = 0.0f;
    Park::sceneryClipForFrame(1080.0f, 8.0f, aspect, clip, eyeX, eyeY, eyeZ, focusX, focusY);
    const float radius = groundFootprintRadius(clip, eyeX, eyeY, focusX, focusY);
    float planes[6][4];
    extractFrustumPlanes(clip, planes);
    ChunkBox here;
    here.minX = focusX - 8.0f;
    here.maxX = focusX + 8.0f;
    here.minY = focusY - 8.0f;
    here.maxY = focusY + 8.0f;
    here.maxZ = 4.0f;
    EXPECT_FALSE(boxOutsideGroundDistance(here, focusX, focusY, radius));
    EXPECT_FALSE(boxOutsideFrustum(here, planes)) << radius;
}

TEST(SceneryScale, TenThousandModelsCycleThroughTheArena)
{
    GeometryArena arena(kSlabVertices, kSlabIndices, kMaxGeometrySlabs);
    LruBudget budget(kDefaultVramBudgetBytes);
    const uint32_t verts = 1200;
    const uint32_t indices = 3600;
    const uint32_t bytes = verts * static_cast<uint32_t>(sizeof(MeshVertex)) + indices * static_cast<uint32_t>(sizeof(uint16_t))
        + 64u * 64u * 4u;
    struct Live
    {
        uint32_t id = 0;
        BufferRange vertices;
        BufferRange indexRange;
    };
    std::vector<Live> live;
    uint32_t evictions = 0;
    uint32_t admitted = 0;
    for (uint32_t id = 0; id < 10000; id++)
    {
        if (verts > kSlabVertices || indices > kSlabIndices || bytes > kDefaultVramBudgetBytes)
        {
            FAIL() << "fixture does not fit one slab";
        }
        BufferRange vertexRange;
        BufferRange indexRange;
        bool placed = false;
        while (!placed)
        {
            if (budget.used() + bytes <= budget.budget() && arena.alloc(verts, indices, vertexRange, indexRange))
            {
                placed = true;
                break;
            }
            const std::optional<uint32_t> dropped = budget.evictOne({});
            if (!dropped.has_value())
            {
                break;
            }
            evictions++;
            for (size_t i = 0; i < live.size(); i++)
            {
                if (live[i].id != *dropped)
                {
                    continue;
                }
                arena.free(live[i].vertices, live[i].indexRange);
                live.erase(live.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
        if (!placed)
        {
            continue;
        }
        budget.insert(id, bytes, id);
        live.push_back(Live{ id, vertexRange, indexRange });
        admitted++;
        EXPECT_LE(budget.used(), budget.budget());
    }
    EXPECT_EQ(admitted, 10000u);
    EXPECT_GT(evictions, 0u);
    EXPECT_LT(live.size(), 10000u);
}

TEST(SceneryScale, PngBombIsRejectedBeforeDecode)
{
    MeshImage image;
    // Signature plus an IHDR that claims 16384x16384. The bitmap is never allocated.
    const uint8_t png[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
                            0x52, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x40, 0x00, 0x08, 0x06, 0x00, 0x00, 0x00, 0xA9,
                            0xC8, 0x10, 0x84, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };
    image.png.assign(png, png + sizeof(png));
    CpuMesh mesh;
    mesh.images.push_back(std::move(image));
    std::string error;
    EXPECT_FALSE(decodeMeshImages(mesh, error));
    EXPECT_NE(error.find("dimensions"), std::string::npos) << error;
    EXPECT_TRUE(mesh.images[0].rgba.empty());
}

TEST(SceneryScale, RepeatUvWrapsAfterInterpolation)
{
    // A 0..2 edge. The fragment at 25% interpolates to 0.5, so GL_REPEAT samples
    // the middle of the texture. Per-vertex fract() stores 0 at both ends.
    const float interpolated = 0.0f + (2.0f - 0.0f) * 0.25f;
    EXPECT_FLOAT_EQ(interpolated, 0.5f);
    EXPECT_FLOAT_EQ(wrapTexcoord(interpolated, false), 0.5f);
    const float collapsed = 0.0f + (0.0f - 0.0f) * 0.25f;
    EXPECT_FLOAT_EQ(collapsed, 0.0f);
    EXPECT_NEAR(mapAtlasUv(0.25f, true, 0.2f, 0.1f), 0.15f, 1.0e-5f);
    EXPECT_FLOAT_EQ(wrapTexcoord(-0.2f, true), 0.0f);
    EXPECT_FLOAT_EQ(wrapTexcoord(1.4f, true), 1.0f);
}

TEST(SceneryScale, HandleCapFallsBackToSpriteNotWhite)
{
    EXPECT_EQ(placementDraw(true, true, false), PlacementDraw::model);
    EXPECT_EQ(placementDraw(true, true, true), PlacementDraw::model);
    EXPECT_EQ(placementDraw(true, false, false), PlacementDraw::sprite);
    EXPECT_EQ(placementDraw(true, false, true), PlacementDraw::sprite);
    EXPECT_EQ(placementDraw(false, false, false), PlacementDraw::sprite);
}

TEST(SceneryScale, RemapColoursReachTheSpriteImage)
{
    const ImageId painted = sceneryRemapImage(ImageId(40), true, 5, true, 9, false, 0);
    EXPECT_EQ(static_cast<uint8_t>(painted.GetPrimary()), 5);
    EXPECT_EQ(static_cast<uint8_t>(painted.GetSecondary()), 9);
    const ImageId ghost = sceneryRemapImage(
        ImageId(40).WithRemap(OpenRCT2::Drawing::FilterPaletteID::paletteGhost), true, 5, false, 0, false, 0);
    EXPECT_TRUE(ghost.IsRemap());
    EXPECT_NE(static_cast<uint8_t>(ghost.GetPrimary()), 5);
}

TEST(SceneryScale, LoaderStopsAndDoesNotResurrectForgottenWork)
{
    std::atomic<int32_t> started{ 0 };
    std::atomic<bool> release{ false };
    {
        ModelLoadQueue queue(
            [&](const std::string&, std::string&) -> std::optional<CpuMesh> {
                started.fetch_add(1);
                while (!release.load())
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                CpuMesh mesh;
                MeshPrimitive primitive;
                primitive.vertices.push_back(MeshVertex{});
                primitive.indices.push_back(0);
                mesh.primitives.push_back(std::move(primitive));
                return mesh;
            },
            true);
        queue.request("held");
        while (started.load() == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        queue.forget("held");
        queue.requestStop();
        release.store(true);
        std::string taken;
        EXPECT_FALSE(queue.takeReady(taken).has_value());
    }
    EXPECT_EQ(started.load(), 1);
}

TEST(SceneryScale, SpriteFaceFollowsTheEye)
{
    EXPECT_EQ(spriteFaceForView(1.0f, 1.0f), 0);
    EXPECT_EQ(spriteFaceForView(-1.0f, 1.0f), 1);
    EXPECT_EQ(spriteFaceForView(-1.0f, -1.0f), 2);
    EXPECT_EQ(spriteFaceForView(1.0f, -1.0f), 3);
}

TEST(SceneryScale, SpriteRunsGroupOneFace)
{
    SceneryChunk chunk;
    const int images[3] = { 4, 4, 9 };
    for (int i = 0; i < 3; i++)
    {
        ChunkPlacement placement;
        placement.x = static_cast<float>(i);
        placement.y = 2.0f;
        placement.z = 1.0f;
        placement.vegetation = i != 2;
        for (int face = 0; face < 4; face++)
        {
            placement.spriteFace[face] = true;
            placement.spriteImage[face] = ImageId(static_cast<uint32_t>(images[i]));
        }
        chunk.placements.push_back(placement);
    }
    buildChunkSpriteRuns(chunk);
    ASSERT_EQ(chunk.spriteInstances.size(), 3u);
    ASSERT_EQ(chunk.spriteRuns[0].size(), 2u);
    const ChunkSpriteRun& first = chunk.spriteRuns[0][0];
    const ChunkSpriteRun& second = chunk.spriteRuns[0][1];
    const ChunkSpriteRun& pair = first.count == 2 ? first : second;
    const ChunkSpriteRun& single = first.count == 2 ? second : first;
    EXPECT_EQ(pair.count, 2u);
    EXPECT_EQ(single.count, 1u);
    EXPECT_TRUE(pair.vegetation);
    EXPECT_FALSE(single.vegetation);
    int32_t total = 0;
    for (const ChunkSpriteRun& run : chunk.spriteRuns[2])
    {
        total += static_cast<int32_t>(run.count);
    }
    EXPECT_EQ(total, 3);
}

TEST(SceneryScale, DistantSpritesCoverTheFarSide)
{
    const float aspect = 16.0f / 9.0f;
    const float zooms[2][2] = { { 1080.0f, 16.0f }, { 1080.0f, 8.0f } };
    const float samples[3][2] = { { 0.0f, 1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f } };
    for (const auto& zoom : zooms)
    {
        const IsoPose pose = makeIsoPose(zoom[0], zoom[1], aspect, 0.0f, 0.0f);
        const float mesh = groundFootprintRadius(pose.clip, pose.eyeX, pose.eyeY, pose.focusX, pose.focusY);
        const float sprites = distantSpriteRadius(pose.clip, pose.focusX, pose.focusY, kDefaultDistantSpriteTiles);
        EXPECT_LT(mesh, sprites);
        EXPECT_GT(groundReachRadius(pose.clip, pose.focusX, pose.focusY), mesh + 100.0f);
        for (const auto& sample : samples)
        {
            float x = 0.0f;
            float y = 0.0f;
            ASSERT_TRUE(groundPlanePoint(pose.clip, sample[0], sample[1], x, y));
            const ChunkBox box = boxAround(x, y);
            EXPECT_TRUE(boxOutsideGroundDistance(box, pose.focusX, pose.focusY, mesh)) << sample[0] << "," << sample[1];
            EXPECT_TRUE(chunkInDistantSprites(box, pose.focusX, pose.focusY, mesh, sprites, pose.planes))
                << sample[0] << "," << sample[1] << " mesh " << mesh << " sprites " << sprites;
        }
        float farX = 0.0f;
        float farY = 0.0f;
        ASSERT_TRUE(groundPlanePoint(pose.clip, 0.0f, 1.0f, farX, farY));
        const float farDistance = std::sqrt(farX * farX + farY * farY);
        const float capped = distantSpriteRadius(pose.clip, pose.focusX, pose.focusY, 200.0f);
        EXPECT_LE(capped, 200.0f);
        const float beyond = 280.0f / farDistance;
        const ChunkBox past = boxAround(farX * beyond, farY * beyond);
        EXPECT_FALSE(chunkInDistantSprites(past, pose.focusX, pose.focusY, mesh, capped, pose.planes));
        const float between = 170.0f / farDistance;
        const ChunkBox ring = boxAround(farX * between, farY * between);
        if (170.0f > mesh)
        {
            EXPECT_TRUE(chunkInDistantSprites(ring, pose.focusX, pose.focusY, mesh, capped, pose.planes));
        }
        const ChunkBox close = boxAround(pose.focusX, pose.focusY + 8.0f);
        EXPECT_FALSE(chunkInDistantSprites(close, pose.focusX, pose.focusY, mesh, sprites, pose.planes));
    }
}

TEST(SceneryScale, HeldRetryRotatesAndLeavesASlot)
{
    const uint32_t keys[2] = { 10u, 20u };
    const uint8_t due[2] = { 1, 1 };
    uint32_t cursor = 0;
    int32_t retries[2] = {};
    int32_t newSlots = 0;
    for (int32_t frame = 0; frame < 8; frame++)
    {
        const int32_t chosen = selectHeldRetry(keys, due, 2, false, cursor);
        ASSERT_GE(chosen, 0);
        ASSERT_LT(chosen, 2);
        retries[chosen]++;
        newSlots += static_cast<int32_t>(kUploadsPerFrame) - 1;
    }
    EXPECT_EQ(retries[0], 4);
    EXPECT_EQ(retries[1], 4);
    EXPECT_EQ(newSlots, 8);

    const uint32_t three[3] = { 1u, 2u, 3u };
    const uint8_t allDue[3] = { 1, 1, 1 };
    cursor = 0;
    int32_t seen[3] = {};
    for (int32_t frame = 0; frame < 6; frame++)
    {
        const int32_t chosen = selectHeldRetry(three, allDue, 3, false, cursor);
        ASSERT_GE(chosen, 0);
        seen[chosen]++;
    }
    EXPECT_EQ(seen[0], 2);
    EXPECT_EQ(seen[1], 2);
    EXPECT_EQ(seen[2], 2);

    const uint8_t skipMiddle[3] = { 1, 0, 1 };
    cursor = 0;
    const int32_t first = selectHeldRetry(three, skipMiddle, 3, false, cursor);
    const int32_t second = selectHeldRetry(three, skipMiddle, 3, false, cursor);
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 2);

    const uint8_t none[2] = { 0, 0 };
    cursor = 0;
    EXPECT_EQ(selectHeldRetry(keys, none, 2, false, cursor), -1);
    EXPECT_EQ(cursor, 0u);
    const int32_t early = selectHeldRetry(keys, none, 2, true, cursor);
    const int32_t earlyNext = selectHeldRetry(keys, none, 2, true, cursor);
    EXPECT_EQ(early, 0);
    EXPECT_EQ(earlyNext, 1);
}

TEST(SceneryScale, LargeParkZoom3SpriteCost)
{
    constexpr int32_t kMap = 256;
    constexpr int32_t kImages = 48;
    const float aspect = 16.0f / 9.0f;
    const float focusX = 128.0f;
    const float focusY = 40.0f;
    const IsoPose pose = makeIsoPose(1080.0f, 8.0f, aspect, focusX, focusY);
    const float mesh = groundFootprintRadius(pose.clip, pose.eyeX, pose.eyeY, pose.focusX, pose.focusY);
    const float mapFar = mapCornerDistance(focusX, focusY, static_cast<float>(kMap), static_cast<float>(kMap));
    const float sprites = distantSpriteRadius(pose.clip, focusX, focusY, std::min(kDefaultDistantSpriteTiles, mapFar));
    EXPECT_NEAR(mesh, 146.0f, 1.5f);
    EXPECT_GT(sprites, mesh);
    EXPECT_LE(sprites, mapFar + 0.1f);

    SceneryChunkGrid grid;
    grid.reset(kMap, kMap);
    for (int32_t cy = 0; cy < grid.chunksY(); cy++)
    {
        for (int32_t cx = 0; cx < grid.chunksX(); cx++)
        {
            SceneryChunk* chunk = grid.at(cx, cy);
            const int32_t x0 = cx * kSceneryChunkTiles;
            const int32_t y0 = cy * kSceneryChunkTiles;
            chunk->box.minX = static_cast<float>(x0);
            chunk->box.minY = static_cast<float>(y0);
            chunk->box.maxX = static_cast<float>(x0 + kSceneryChunkTiles);
            chunk->box.maxY = static_cast<float>(y0 + kSceneryChunkTiles);
            chunk->box.minZ = 0.0f;
            chunk->box.maxZ = 4.0f;
            chunk->placements.reserve(static_cast<size_t>(kSceneryChunkTiles * kSceneryChunkTiles));
            for (int32_t y = y0; y < y0 + kSceneryChunkTiles; y++)
            {
                for (int32_t x = x0; x < x0 + kSceneryChunkTiles; x++)
                {
                    ChunkPlacement placement;
                    placement.x = static_cast<float>(x) + 0.5f;
                    placement.y = static_cast<float>(y) + 0.5f;
                    placement.z = 0.5f;
                    const uint32_t image = static_cast<uint32_t>((x * 17 + y) % kImages);
                    for (int face = 0; face < 4; face++)
                    {
                        placement.spriteFace[face] = true;
                        placement.spriteImage[face] = ImageId(image);
                    }
                    chunk->placements.push_back(placement);
                }
            }
            buildChunkSpriteRuns(*chunk);
            chunk->built = true;
            chunk->dirty = false;
        }
    }

    const int face = spriteFaceForView(pose.eyeX - pose.focusX, pose.eyeY - pose.focusY);
    std::vector<std::vector<ChunkSpritePoint>> pages(static_cast<size_t>(kSpriteAtlasPageBudget));
    int32_t spriteChunks = 0;
    int32_t instances = 0;
    auto gather = [&]() {
        spriteChunks = 0;
        instances = 0;
        for (auto& page : pages)
        {
            page.clear();
        }
        int32_t lastChunk = -1;
        appendDistantPlacements(
            grid, pose.planes, focusX, focusY, mesh, sprites, face, [&](const SceneryChunk& chunk, const ChunkSpriteRun& run) {
                const int32_t chunkId = static_cast<int32_t>(&chunk - grid.at(0, 0));
                if (chunkId != lastChunk)
                {
                    lastChunk = chunkId;
                    spriteChunks++;
                }
                const int32_t page = static_cast<int32_t>(run.image.GetIndex() % static_cast<uint32_t>(kSpriteAtlasPageBudget));
                auto& batch = pages[static_cast<size_t>(page)];
                batch.reserve(batch.size() + static_cast<size_t>(run.count));
                const std::vector<uint32_t>& order = chunk.spriteOrder[face];
                for (uint32_t n = 0; n < run.count; n++)
                {
                    batch.push_back(chunk.spriteInstances[order[run.begin + n]]);
                }
                instances += static_cast<int32_t>(run.count);
            });
    };
    gather();
    const auto started = std::chrono::steady_clock::now();
    gather();
    const int64_t microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count();
    int32_t draws = 0;
    for (const auto& page : pages)
    {
        if (!page.empty())
        {
            draws++;
        }
    }
    int32_t meshChunks = 0;
    for (int32_t cy = 0; cy < grid.chunksY(); cy++)
    {
        for (int32_t cx = 0; cx < grid.chunksX(); cx++)
        {
            const SceneryChunk* chunk = grid.at(cx, cy);
            if (!boxOutsideGroundDistance(chunk->box, focusX, focusY, mesh) && !boxOutsideFrustum(chunk->box, pose.planes))
            {
                meshChunks++;
            }
        }
    }
    EXPECT_GT(spriteChunks, 0);
    EXPECT_EQ(instances, spriteChunks * kSceneryChunkTiles * kSceneryChunkTiles);
    EXPECT_EQ(draws, static_cast<int32_t>(kSpriteAtlasPageBudget));
    EXPECT_LT(meshChunks, 256);
    std::printf(
        "DISTANT_SPRITE_FRAME map=%d zoom=3 focus=(%.0f,%.0f) meshRadius=%.1f spriteRadius=%.1f meshChunks=%d spriteChunks=%d "
        "instances=%d draws=%d us=%lld\n",
        kMap, focusX, focusY, static_cast<double>(mesh), static_cast<double>(sprites), meshChunks, spriteChunks, instances,
        draws, static_cast<long long>(microseconds));
}

TEST(SceneryScale, SpriteShareStaysCapped)
{
    EXPECT_FALSE(spriteOverShare(10, kMaxSpriteImages, 100, kMaxSpriteAtlasTexels, 0, kMaxSpriteDedicated));
    EXPECT_TRUE(spriteOverShare(kMaxSpriteImages, kMaxSpriteImages, 0, kMaxSpriteAtlasTexels, 0, kMaxSpriteDedicated));
    EXPECT_TRUE(
        spriteOverShare(1, kMaxSpriteImages, kMaxSpriteAtlasTexels + 1u, kMaxSpriteAtlasTexels, 0, kMaxSpriteDedicated));
    EXPECT_TRUE(spriteOverShare(1, kMaxSpriteImages, 0, kMaxSpriteAtlasTexels, kMaxSpriteDedicated + 1u, kMaxSpriteDedicated));
}

TEST(SceneryScale, StaleSpriteDropsAndThisFrameStays)
{
    const uint64_t lastUsed[3] = { 10, 400, 500 };
    const uint8_t used[3] = { 0, 0, 1 };
    EXPECT_EQ(pickSpriteToDrop(lastUsed, used, 3, 500, kSpriteStaleFrames, true), 0);
    const uint8_t allFresh[3] = { 0, 0, 0 };
    const uint64_t recent[3] = { 490, 495, 499 };
    EXPECT_EQ(pickSpriteToDrop(recent, allFresh, 3, 500, kSpriteStaleFrames, true), -1);
    EXPECT_EQ(pickSpriteToDrop(lastUsed, used, 3, 500, kSpriteStaleFrames, false), 0);
    const uint8_t allUsed[2] = { 1, 1 };
    const uint64_t old[2] = { 1, 2 };
    EXPECT_EQ(pickSpriteToDrop(old, allUsed, 2, 500, kSpriteStaleFrames, false), -1);
}

TEST(SceneryScale, ModelTakesAColdSpriteBeforeItFails)
{
    const uint64_t lastUsed[2] = { 10, 500 };
    const uint8_t used[2] = { 0, 1 };
    const int32_t dropped = pickSpriteToDrop(lastUsed, used, 2, 500, kSpriteStaleFrames, false);
    EXPECT_EQ(dropped, 0);
    const uint8_t onlyVisible[1] = { 1 };
    const uint64_t visible[1] = { 1 };
    EXPECT_EQ(pickSpriteToDrop(visible, onlyVisible, 1, 500, kSpriteStaleFrames, false), -1);
}

TEST(SceneryScale, FreedSpriteIdWaitsUntilNextFrame)
{
    SpriteIdPool pool;
    pool.release(3);
    pool.release(7);
    EXPECT_EQ(pool.acquire(), -1);
    pool.nextFrame();
    EXPECT_EQ(pool.acquire(), 7);
    EXPECT_EQ(pool.acquire(), 3);
    EXPECT_EQ(pool.acquire(), -1);
}

namespace
{
    void mulClipLocal(float out[16], const float proj[16], const float view[16])
    {
        for (int32_t col = 0; col < 4; col++)
        {
            for (int32_t row = 0; row < 4; row++)
            {
                float sum = 0.0f;
                for (int32_t k = 0; k < 4; k++)
                {
                    sum += proj[row + k * 4] * view[k + col * 4];
                }
                out[row + col * 4] = sum;
            }
        }
    }

    // ParkView's camera: bx left-handed lookAt, the X row negated, bx::mtxProj in degrees.
    void gameClip(
        float clip[16], float eyeX, float eyeY, float eyeZ, float forwardX, float forwardY, float forwardZ, float aspect)
    {
        const float fl = std::sqrt(forwardX * forwardX + forwardY * forwardY + forwardZ * forwardZ);
        const float vx = forwardX / fl;
        const float vy = forwardY / fl;
        const float vz = forwardZ / fl;
        const float ux = 0.0f;
        const float uy = 0.0f;
        const float uz = 1.0f;
        float rx = uy * vz - uz * vy;
        float ry = uz * vx - ux * vz;
        float rz = ux * vy - uy * vx;
        const float rl = std::sqrt(rx * rx + ry * ry + rz * rz);
        rx /= rl;
        ry /= rl;
        rz /= rl;
        const float upX = vy * rz - vz * ry;
        const float upY = vz * rx - vx * rz;
        const float upZ = vx * ry - vy * rx;
        float view[16] = {};
        view[0] = rx;
        view[1] = upX;
        view[2] = vx;
        view[4] = ry;
        view[5] = upY;
        view[6] = vy;
        view[8] = rz;
        view[9] = upZ;
        view[10] = vz;
        view[12] = -(rx * eyeX + ry * eyeY + rz * eyeZ);
        view[13] = -(upX * eyeX + upY * eyeY + upZ * eyeZ);
        view[14] = -(vx * eyeX + vy * eyeY + vz * eyeZ);
        view[15] = 1.0f;
        view[0] = -view[0];
        view[4] = -view[4];
        view[8] = -view[8];
        view[12] = -view[12];
        const float height = 1.0f / std::tan(50.0f * 0.5f * 3.14159265f / 180.0f);
        const float width = height / aspect;
        const float nearPlane = 0.2f;
        const float farPlane = 8000.0f;
        const float diff = farPlane - nearPlane;
        float proj[16] = {};
        proj[0] = width;
        proj[5] = height;
        proj[10] = (farPlane + nearPlane) / diff;
        proj[11] = 1.0f;
        proj[14] = -(2.0f * farPlane * nearPlane) / diff;
        mulClipLocal(clip, proj, view);
    }
} // namespace

TEST(SceneryScale, GameCameraKeepsTheFocusChunk)
{
    const float aspect = 16.0f / 9.0f;
    const float targetX = 128.0f;
    const float targetY = 96.0f;
    const float targetZ = 1.0f;
    const float dir = 0.70710678f;
    const float distance = (1.0f / std::tan(25.0f * 3.14159265f / 180.0f)) * 1.41421356f * 1080.0f / (2.0f * 32.0f) * 0.8f;
    const float horizontal = distance * std::cos(0.52359878f);
    const float vertical = distance * std::sin(0.52359878f);
    const float eyeX = targetX + dir * horizontal;
    const float eyeY = targetY + dir * horizontal;
    const float eyeZ = targetZ + vertical;
    const float yaw = std::atan2(-dir, -dir);
    const float pitch = -0.52359878f;
    const float forwardX = std::cos(yaw) * std::cos(pitch);
    const float forwardY = std::sin(yaw) * std::cos(pitch);
    const float forwardZ = std::sin(pitch);
    float clip[16];
    gameClip(clip, eyeX, eyeY, eyeZ, forwardX, forwardY, forwardZ, aspect);
    const float t = -eyeZ / forwardZ;
    const float focusX = eyeX + forwardX * t;
    const float focusY = eyeY + forwardY * t;
    float planes[6][4];
    extractFrustumPlanes(clip, planes);
    const float radius = groundFootprintRadius(clip, eyeX, eyeY, focusX, focusY);
    ChunkBox focus = boxAround(targetX, targetY);
    focus.minZ = 0.0f;
    focus.maxZ = targetZ + 4.0f;
    EXPECT_GT(radius, 40.0f);
    EXPECT_LT(radius, 160.0f);
    EXPECT_FALSE(boxOutsideGroundDistance(focus, focusX, focusY, radius));
    EXPECT_FALSE(boxOutsideFrustum(focus, planes));
    EXPECT_FALSE(
        chunkInDistantSprites(focus, focusX, focusY, radius, distantSpriteRadius(clip, focusX, focusY, 1280.0f), planes));
}

TEST(SceneryScale, HorizonMissFallsBackToTheFarLimit)
{
    const float aspect = 16.0f / 9.0f;
    const float eyeX = 0.0f;
    const float eyeY = -30.0f;
    const float eyeZ = 40.0f;
    const float pitch = -0.05f;
    const float forwardX = 0.0f;
    const float forwardY = std::cos(pitch);
    const float forwardZ = std::sin(pitch);
    float clip[16];
    gameClip(clip, eyeX, eyeY, eyeZ, forwardX, forwardY, forwardZ, aspect);
    float missX = 0.0f;
    float missY = 0.0f;
    EXPECT_FALSE(groundPlanePoint(clip, 0.0f, 1.0f, missX, missY));
    const float t = -eyeZ / forwardZ;
    const float focusX = eyeX + forwardX * t;
    const float focusY = eyeY + forwardY * t;
    const float mesh = groundFootprintRadius(clip, eyeX, eyeY, focusX, focusY);
    const float reach = groundReachRadius(clip, focusX, focusY);
    EXPECT_GE(reach, kDefaultDistantSpriteTiles);
    EXPECT_LT(mesh, reach);
    float planes[6][4];
    extractFrustumPlanes(clip, planes);
    const float sprites = distantSpriteRadius(clip, focusX, focusY, kDefaultDistantSpriteTiles);
    const float along = std::min(600.0f, sprites - 20.0f);
    const float nx = forwardX / std::sqrt(forwardX * forwardX + forwardY * forwardY);
    const float ny = forwardY / std::sqrt(forwardX * forwardX + forwardY * forwardY);
    ChunkBox farBox = boxAround(focusX + nx * along, focusY + ny * along);
    if (along > mesh)
    {
        EXPECT_TRUE(boxOutsideGroundDistance(farBox, focusX, focusY, mesh));
        EXPECT_FALSE(boxOutsideGroundDistance(farBox, focusX, focusY, sprites));
    }
}

namespace
{
    bool solidSprite(int32_t& width, int32_t& height, const uint8_t*& rgba)
    {
        static const uint8_t kPixels[16] = { 255, 32, 32, 255, 255, 32, 32, 255, 255, 32, 32, 255, 255, 32, 32, 255 };
        width = 2;
        height = 2;
        rgba = kPixels;
        return true;
    }

    void writeSceneryObject(const std::string& path, const std::string& id)
    {
        const std::string json = "{\n"
                                 "  \"id\": \""
            + id
            + "\",\n"
              "  \"authors\": [\"OpenRCT2 3D test\"],\n"
              "  \"version\": \"1.0\",\n"
              "  \"sourceGame\": \"custom\",\n"
              "  \"objectType\": \"scenery_small\",\n"
              "  \"properties\": {\n"
              "    \"height\": 32,\n"
              "    \"price\": 10,\n"
              "    \"removalPrice\": 10,\n"
              "    \"cursor\": \"treeDown\",\n"
              "    \"isTree\": true,\n"
              "    \"shape\": \"4/4\"\n"
              "  }\n"
              "}\n";
        OpenRCT2::File::WriteAllBytes(path, json.data(), json.size());
    }

    std::string tempRoot()
    {
#ifdef _WIN32
        wchar_t buffer[MAX_PATH + 1] = {};
        const DWORD length = GetTempPathW(MAX_PATH, buffer);
        std::string path = length == 0 ? "." : OpenRCT2::String::toUtf8(std::wstring_view(buffer, length));
#else
        const char* env = std::getenv("TMPDIR");
        std::string path = (env != nullptr && env[0] != '\0') ? env : "/tmp";
#endif
        while (!path.empty() && (path.back() == '/' || path.back() == '\\'))
        {
            path.pop_back();
        }
        return path;
    }

    void placeScenery(OpenRCT2::IObjectManager& objects, std::string_view id, const CoordsXYZD& loc)
    {
        OpenRCT2::Object* object = objects.LoadObject(id);
        if (object == nullptr)
        {
            ADD_FAILURE() << id;
            return;
        }
        const OpenRCT2::ObjectEntryIndex index = objects.GetLoadedObjectEntryIndex(object);
        OpenRCT2::GameActions::SmallSceneryPlaceAction action(
            loc, 0, index, OpenRCT2::Drawing::Colour::brightGreen, OpenRCT2::Drawing::Colour::brightGreen,
            OpenRCT2::Drawing::Colour::brightGreen);
        // Execute queues the action until the game update. Nested runs it now.
        const auto result = OpenRCT2::GameActions::ExecuteNested(&action, OpenRCT2::getGameState());
        if (result.error != OpenRCT2::GameActions::Status::ok)
        {
            ADD_FAILURE() << id << " place failed " << static_cast<int>(result.error);
        }
    }
} // namespace

TEST(SceneryScale, DefaultCameraDrawsParkScenery)
{
    // View3D's back buffer starts at 1280x720. With no viewport, zoom 0 is 64 px per tile.
    constexpr uint16_t kBackWidth = 1280;
    constexpr uint16_t kBackHeight = 720;
    const std::string modelId = "rct2.scenery_small.tcrp";
    const std::string spriteId = "test.scenery_small.bush";

    const std::string root = tempRoot();
    const std::string dataRoot = OpenRCT2::Path::Combine(root, "o3d-default-camera-data");
    const std::string source = OpenRCT2::Path::Combine(root, "o3d-default-camera-src");
    const std::string cache = OpenRCT2::Path::Combine(root, "o3d-default-camera-cache");
    // Windows runs every test in one process. Put the globals and bgfx back before the next test.
    struct Cleanup
    {
        bool headless = gOpenRCT2Headless;
        bool noGraphics = gOpenRCT2NoGraphics;
        u8string dataPath = gCustomOpenRCT2DataPath;
        bool device = false;
        std::string dataRoot;
        std::string source;
        std::string cache;

        Cleanup(std::string dataRoot_, std::string source_, std::string cache_)
            : dataRoot(std::move(dataRoot_))
            , source(std::move(source_))
            , cache(std::move(cache_))
        {
        }

        ~Cleanup()
        {
            if (device)
            {
                Park::setScenerySpriteOverride(nullptr);
                Park::releaseScenery();
                Park::shutdownSceneryTestDevice();
            }
            gOpenRCT2Headless = headless;
            gOpenRCT2NoGraphics = noGraphics;
            gCustomOpenRCT2DataPath = dataPath;
            OpenRCT2::Path::DeleteDirectory(dataRoot);
            OpenRCT2::Path::DeleteDirectory(source);
            OpenRCT2::Path::DeleteDirectory(cache);
        }
    } cleanup(dataRoot, source, cache);
    OpenRCT2::Path::DeleteDirectory(dataRoot);
    OpenRCT2::Path::DeleteDirectory(source);
    OpenRCT2::Path::DeleteDirectory(cache);
    OpenRCT2::Path::CreateDirectory(OpenRCT2::Path::Combine(dataRoot, "object"));
    OpenRCT2::Path::CreateDirectory(OpenRCT2::Path::Combine(dataRoot, "language"));
    OpenRCT2::Path::CreateDirectory(source);
    OpenRCT2::Path::CreateDirectory(cache);
    std::string language = "../data/language";
    if (!OpenRCT2::File::Exists(OpenRCT2::Path::Combine(language, "en-US.txt")))
    {
        language = "data/language";
    }
    for (const char* name : { "en-US.txt", "en-GB.txt" })
    {
        const auto bytes = OpenRCT2::File::ReadAllBytes(OpenRCT2::Path::Combine(language, name));
        OpenRCT2::File::WriteAllBytes(OpenRCT2::Path::Combine(dataRoot, "language", name), bytes.data(), bytes.size());
    }
    writeSceneryObject(OpenRCT2::Path::Combine(dataRoot, "object", modelId + ".json"), modelId);
    writeSceneryObject(OpenRCT2::Path::Combine(dataRoot, "object", spriteId + ".json"), spriteId);

    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    gCustomOpenRCT2DataPath = dataRoot;
    auto context = OpenRCT2::CreateContext();
    ASSERT_NE(context, nullptr);
    ASSERT_TRUE(context->Initialise());
    OpenRCT2::gameStateInitAll(OpenRCT2::getGameState(), TileCoordsXY{ 32, 32 });
    OpenRCT2::getGameState().cheats.sandboxMode = true;
    OpenRCT2::getGameState().cheats.disableClearanceChecks = true;
    // Centre tiles. The default camera, with no viewport, looks at the middle of the map.
    placeScenery(context->GetObjectManager(), modelId, CoordsXYZD{ 16 * 32, 16 * 32, 0, 0 });
    placeScenery(context->GetObjectManager(), spriteId, CoordsXYZD{ 17 * 32, 16 * 32, 0, 0 });
    // GfxGetG1Element asserts while this is set. These objects have no g1, so the lookup misses
    // and the override supplies the X-sprite pixels.
    gOpenRCT2NoGraphics = false;

    const std::string glb = OpenRCT2::Path::Combine(TestData::GetBasePath(), "object-models", "rct2.scenery_small.tcrp.glb");
    const auto glbBytes = OpenRCT2::File::ReadAllBytes(glb);
    OpenRCT2::File::WriteAllBytes(OpenRCT2::Path::Combine(source, modelId + ".glb"), glbBytes.data(), glbBytes.size());
    const ModelCacheStats prepared = prepareModelCache({ source }, cache, nullptr);
    EXPECT_EQ(prepared.converted + prepared.skipped, 1);
    EXPECT_TRUE(modelIsValidated(modelId));
    EXPECT_FALSE(modelIsValidated(spriteId));

    ASSERT_TRUE(Park::initSceneryTestDevice(kBackWidth, kBackHeight));
    cleanup.device = true;
    Park::releaseScenery();
    Park::setScenerySpriteOverride(&solidSprite);
    Park::resetSceneryFrameStats();
    Park::poseDefaultCamera(kBackWidth, kBackHeight);

    float eyeX = 0.0f;
    float eyeY = 0.0f;
    float eyeZ = 0.0f;
    float clip[16];
    ASSERT_TRUE(Park::copyCamera(eyeX, eyeY, eyeZ, clip));
    float focusX = 0.0f;
    float focusY = 0.0f;
    ASSERT_TRUE(Park::copyViewFocus(focusX, focusY));
    const float radius = groundFootprintRadius(clip, eyeX, eyeY, focusX, focusY);
    float planes[6][4];
    extractFrustumPlanes(clip, planes);
    ChunkBox focusChunk;
    focusChunk.minX = focusX - 8.0f;
    focusChunk.maxX = focusX + 8.0f;
    focusChunk.minY = focusY - 8.0f;
    focusChunk.maxY = focusY + 8.0f;
    focusChunk.maxZ = 4.0f;
    EXPECT_GT(radius, 16.0f);
    EXPECT_FALSE(boxOutsideGroundDistance(focusChunk, focusX, focusY, radius));
    EXPECT_FALSE(boxOutsideFrustum(focusChunk, planes)) << radius;

    int32_t sceneryTiles = 0;
    const TileCoordsXY mapSize = OpenRCT2::getGameState().mapSize;
    for (int32_t y = 0; y < mapSize.y; y++)
    {
        for (int32_t x = 0; x < mapSize.x; x++)
        {
            for (const OpenRCT2::SmallSceneryElement* element :
                 OpenRCT2::TileElementsView<OpenRCT2::SmallSceneryElement>(TileCoordsXY{ x, y }))
            {
                if (element != nullptr)
                {
                    sceneryTiles++;
                }
            }
        }
    }
    EXPECT_GT(sceneryTiles, 0) << mapSize.x << "x" << mapSize.y;

    // The model load is a worker thread. A tight frame loop can finish before it is scheduled.
    for (int32_t frame = 0; frame < 200 && Park::sceneryFrameStats().modelSubmits == 0; frame++)
    {
        Park::drawScenery();
        Park::sceneryTestFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const Park::SceneryFrameStats drawn = Park::sceneryFrameStats();
    EXPECT_GT(drawn.placements, 0);
    EXPECT_GT(drawn.modelInstances, 0);
    EXPECT_GT(drawn.modelSubmits, 0);
    EXPECT_GT(drawn.spriteInstances, 0);
    EXPECT_GT(drawn.spriteSubmits, 0);
}

TEST(SceneryScale, DefaultCameraDrawsDistantSprites)
{
    // 1280x720, zoom 3 is 8 px per tile. The 3D radius is about 100 tiles, so the map has to
    // extend past that or the distant layer has nothing to draw.
    constexpr uint16_t kBackWidth = 1280;
    constexpr uint16_t kBackHeight = 720;
    constexpr int32_t kMapTiles = 240;
    constexpr float kTilePixels = 8.0f;
    const std::string spriteId = "test.scenery_small.far";

    const std::string root = tempRoot();
    const std::string dataRoot = OpenRCT2::Path::Combine(root, "o3d-distant-camera-data");
    const std::string source = OpenRCT2::Path::Combine(root, "o3d-distant-camera-src");
    const std::string cache = OpenRCT2::Path::Combine(root, "o3d-distant-camera-cache");
    struct Cleanup
    {
        bool headless = gOpenRCT2Headless;
        bool noGraphics = gOpenRCT2NoGraphics;
        u8string dataPath = gCustomOpenRCT2DataPath;
        bool device = false;
        std::string dataRoot;
        std::string source;
        std::string cache;

        Cleanup(std::string dataRoot_, std::string source_, std::string cache_)
            : dataRoot(std::move(dataRoot_))
            , source(std::move(source_))
            , cache(std::move(cache_))
        {
        }

        ~Cleanup()
        {
            if (device)
            {
                Park::setScenerySpriteOverride(nullptr);
                Park::releaseScenery();
                Park::shutdownSceneryTestDevice();
            }
            gOpenRCT2Headless = headless;
            gOpenRCT2NoGraphics = noGraphics;
            gCustomOpenRCT2DataPath = dataPath;
            OpenRCT2::Path::DeleteDirectory(dataRoot);
            OpenRCT2::Path::DeleteDirectory(source);
            OpenRCT2::Path::DeleteDirectory(cache);
        }
    } cleanup(dataRoot, source, cache);
    OpenRCT2::Path::DeleteDirectory(dataRoot);
    OpenRCT2::Path::DeleteDirectory(source);
    OpenRCT2::Path::DeleteDirectory(cache);
    OpenRCT2::Path::CreateDirectory(OpenRCT2::Path::Combine(dataRoot, "object"));
    OpenRCT2::Path::CreateDirectory(OpenRCT2::Path::Combine(dataRoot, "language"));
    OpenRCT2::Path::CreateDirectory(source);
    OpenRCT2::Path::CreateDirectory(cache);
    std::string language = "../data/language";
    if (!OpenRCT2::File::Exists(OpenRCT2::Path::Combine(language, "en-US.txt")))
    {
        language = "data/language";
    }
    for (const char* name : { "en-US.txt", "en-GB.txt" })
    {
        const auto bytes = OpenRCT2::File::ReadAllBytes(OpenRCT2::Path::Combine(language, name));
        OpenRCT2::File::WriteAllBytes(OpenRCT2::Path::Combine(dataRoot, "language", name), bytes.data(), bytes.size());
    }
    writeSceneryObject(OpenRCT2::Path::Combine(dataRoot, "object", spriteId + ".json"), spriteId);

    gOpenRCT2Headless = true;
    gOpenRCT2NoGraphics = true;
    gCustomOpenRCT2DataPath = dataRoot;
    auto context = OpenRCT2::CreateContext();
    ASSERT_NE(context, nullptr);
    ASSERT_TRUE(context->Initialise());
    OpenRCT2::gameStateInitAll(OpenRCT2::getGameState(), TileCoordsXY{ kMapTiles, kMapTiles });
    OpenRCT2::getGameState().cheats.sandboxMode = true;
    OpenRCT2::getGameState().cheats.disableClearanceChecks = true;

    const float target = static_cast<float>(kMapTiles) * 0.5f;
    Park::poseIsoCamera(kBackWidth, kBackHeight, kTilePixels, target, target);
    float eyeX = 0.0f;
    float eyeY = 0.0f;
    float eyeZ = 0.0f;
    float clip[16];
    ASSERT_TRUE(Park::copyCamera(eyeX, eyeY, eyeZ, clip));
    float focusX = 0.0f;
    float focusY = 0.0f;
    ASSERT_TRUE(Park::copyViewFocus(focusX, focusY));
    const float meshRadius = groundFootprintRadius(clip, eyeX, eyeY, focusX, focusY);
    const float mapFar = mapCornerDistance(focusX, focusY, static_cast<float>(kMapTiles), static_cast<float>(kMapTiles));
    const float spriteRadius = distantSpriteRadius(clip, focusX, focusY, std::min(kDefaultDistantSpriteTiles, mapFar));
    float planes[6][4];
    extractFrustumPlanes(clip, planes);
    const float dx = focusX - eyeX;
    const float dy = focusY - eyeY;
    const float length = std::sqrt(dx * dx + dy * dy);
    ASSERT_GT(length, 1.0f);
    int32_t tileX = -1;
    int32_t tileY = -1;
    for (float distance = meshRadius + 24.0f; distance < spriteRadius - 8.0f; distance += 8.0f)
    {
        const float x = focusX + dx / length * distance;
        const float y = focusY + dy / length * distance;
        ChunkBox box = boxAround(x, y);
        if (!boxOutsideGroundDistance(box, focusX, focusY, meshRadius))
        {
            continue;
        }
        if (boxOutsideGroundDistance(box, focusX, focusY, spriteRadius) || boxOutsideFrustum(box, planes))
        {
            continue;
        }
        const int32_t ix = static_cast<int32_t>(std::floor(x));
        const int32_t iy = static_cast<int32_t>(std::floor(y));
        if (ix > 1 && iy > 1 && ix < kMapTiles - 2 && iy < kMapTiles - 2)
        {
            tileX = ix;
            tileY = iy;
            break;
        }
    }
    ASSERT_GE(tileX, 0) << meshRadius << " " << spriteRadius;
    placeScenery(context->GetObjectManager(), spriteId, CoordsXYZD{ tileX * 32, tileY * 32, 0, 0 });
    gOpenRCT2NoGraphics = false;

    ASSERT_TRUE(Park::initSceneryTestDevice(kBackWidth, kBackHeight));
    cleanup.device = true;
    Park::releaseScenery();
    Park::setScenerySpriteOverride(&solidSprite);
    Park::poseIsoCamera(kBackWidth, kBackHeight, kTilePixels, target, target);
    Park::resetSceneryFrameStats();
    for (int32_t frame = 0; frame < 400 && Park::sceneryFrameStats().distantSpriteSubmits == 0; frame++)
    {
        Park::drawScenery();
        Park::sceneryTestFrame();
    }
    const Park::SceneryFrameStats drawn = Park::sceneryFrameStats();
    EXPECT_GT(drawn.distantSpriteSubmits, 0);
    EXPECT_GT(drawn.distantSpriteInstances, 0);
    EXPECT_EQ(drawn.modelSubmits, 0);
}
