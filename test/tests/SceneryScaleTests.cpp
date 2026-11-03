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
#include "openrct2-3d/src/ViewerScale.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
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
    // ParkView::commitView, including bx::mtxMul's argument order.
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
} // namespace

TEST(SceneryScale, ZoomedOutRadiusStaysBounded)
{
    const float aspect = 16.0f / 9.0f;
    const float hdZoom2 = zoomRadius(1080.0f, 16.0f, aspect);
    const float hdZoom3 = zoomRadius(1080.0f, 8.0f, aspect);
    const float uhdZoom3 = zoomRadius(2160.0f, 8.0f, aspect);
    EXPECT_GT(hdZoom2, 68.0f);
    EXPECT_LT(hdZoom2, 120.0f);
    EXPECT_GT(hdZoom3, 136.0f);
    EXPECT_LT(hdZoom3, 220.0f);
    EXPECT_GT(uhdZoom3, 270.0f);
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
