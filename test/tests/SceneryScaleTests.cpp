/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "openrct2-3d/src/GlbMesh.hpp"
#include "openrct2-3d/src/GpuBudget.hpp"
#include "openrct2-3d/src/ModelLoadQueue.hpp"
#include "openrct2-3d/src/SceneryChunk.hpp"
#include "openrct2-3d/src/SceneryDraw.hpp"
#include "openrct2-3d/src/SceneryScale.hpp"
#include "openrct2-3d/src/ViewerScale.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <openrct2/drawing/FilterPaletteIds.h>
#include <openrct2/world/Location.hpp>
#include <openrct2/world/Scenery.h>
#include <string>
#include <thread>
#include <unordered_set>

using namespace OpenRCT2::View3D;

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

TEST(SceneryScale, ZoomedOutGroundDistanceStaysVisible)
{
    ChunkBox atFocus;
    atFocus.maxX = 16.0f;
    atFocus.maxY = 16.0f;
    atFocus.maxZ = 4.0f;
    // Zoom 3 puts the eye about 164 tiles from the focus. A 3D test from that eye culls the focus.
    EXPECT_TRUE(boxOutsideDistance(atFocus, 0.0f, -164.0f, 80.0f, kDefaultViewDistanceTiles));
    const float radius = sceneryCullRadius(0.0f, -164.0f, 0.0f, 0.0f);
    EXPECT_GE(radius, 164.0f);
    EXPECT_FALSE(boxOutsideGroundDistance(atFocus, 0.0f, 0.0f, radius));
    ChunkBox far;
    far.minX = 400.0f;
    far.maxX = 416.0f;
    far.maxY = 16.0f;
    EXPECT_TRUE(boxOutsideGroundDistance(far, 0.0f, 0.0f, radius));
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
        release.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        std::string taken;
        EXPECT_FALSE(queue.takeReady(taken).has_value());
        EXPECT_EQ(queue.phase("held"), ModelLoadPhase::absent);
    }
    std::atomic<int32_t> loads{ 0 };
    const auto startedAt = std::chrono::steady_clock::now();
    {
        ModelLoadQueue queue(
            [&](const std::string&, std::string&) -> std::optional<CpuMesh> {
                loads.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                return CpuMesh{};
            },
            true);
        for (int32_t i = 0; i < 20; i++)
        {
            queue.request("m" + std::to_string(i));
        }
        while (loads.load() == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    const auto elapsed = std::chrono::steady_clock::now() - startedAt;
    EXPECT_LT(elapsed, std::chrono::milliseconds(400));
    EXPECT_LT(loads.load(), 20);
}
