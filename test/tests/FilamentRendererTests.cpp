/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include <openrct2-3d/src/FilamentRenderer.hpp>
#include <openrct2-3d/src/GpuBudget.hpp>
#include <openrct2-3d/src/MeshDraw.hpp>
#include <openrct2-3d/src/PaletteComposite.hpp>

#include <array>
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>

using namespace OpenRCT2::View3D;

TEST(FilamentRenderer, IndexedUiPreservesViewportAndHonoursPitch)
{
    std::array<ColourBGRA, 256> palette{};
    palette[1] = { 10, 20, 30, 0 };
    palette[2] = { 40, 50, 60, 0 };
    const uint8_t indexed[] = { 1, 0, 255, 2, 1, 255 };
    std::array<uint8_t, 16> rgba;
    rgba.fill(77);
    ASSERT_TRUE(compositeIndexedUi(indexed, 2, 2, 3, palette.data(), rgba.data()));
    const std::array<uint8_t, 16> expected = { 30, 20, 10, 255, 77, 77, 77, 77,
        60, 50, 40, 255, 30, 20, 10, 255 };
    EXPECT_EQ(rgba, expected);
}

TEST(FilamentRenderer, IndexedUiRejectsInvalidBuffersAndDimensions)
{
    std::array<ColourBGRA, 256> palette{};
    const uint8_t indexed[] = { 1, 2 };
    uint8_t rgba[8]{};
    EXPECT_FALSE(compositeIndexedUi(nullptr, 2, 1, 2, palette.data(), rgba));
    EXPECT_FALSE(compositeIndexedUi(indexed, 2, 1, 2, nullptr, rgba));
    EXPECT_FALSE(compositeIndexedUi(indexed, 2, 1, 2, palette.data(), nullptr));
    EXPECT_FALSE(compositeIndexedUi(indexed, 2, 1, 1, palette.data(), rgba));
    EXPECT_FALSE(compositeIndexedUi(indexed, 0, 1, 2, palette.data(), rgba));
    EXPECT_FALSE(compositeIndexedUi(indexed, 2, -1, 2, palette.data(), rgba));
}

TEST(FilamentRenderer, InstanceTransformPreservesTranslationAndYaw)
{
    const auto transform = Gpu::instanceTransform(5.0f, 7.0f, 9.0f, 1.57079632679f);
    const auto position = transform * filament::math::float4{ 1, 0, 0, 1 };
    EXPECT_NEAR(position.x, 5.0f, 0.00001f);
    EXPECT_NEAR(position.y, 8.0f, 0.00001f);
    EXPECT_NEAR(position.z, 9.0f, 0.00001f);
    EXPECT_FLOAT_EQ(position.w, 1.0f);
}

TEST(FilamentRenderer, CameraConversionPreservesCpuClipCoordinates)
{
    const float view[] = { -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 3, -4, -5, 1 };
    const float projection[] = { 1.5f, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1.1f, 1, 0, 0, -0.2f, 0 };
    filament::math::mat4f originalView;
    filament::math::mat4f originalProjection;
    std::memcpy(&originalView, view, sizeof(view));
    std::memcpy(&originalProjection, projection, sizeof(projection));
    filament::math::mat4f model;
    filament::math::mat4f adjustedProjection;
    Gpu::cameraMatrices(view, projection, model, adjustedProjection);
    const auto expected = originalProjection * originalView;
    const auto actual = adjustedProjection * inverse(model);
    for (size_t column = 0; column < 4; ++column)
    {
        for (size_t row = 0; row < 4; ++row)
        {
            EXPECT_NEAR(expected[column][row], actual[column][row], 0.00001f);
        }
    }
    EXPECT_LT(adjustedProjection[2][3], 0.0f);
}

TEST(FilamentRenderer, GeometryBoundsIncludeEveryVertex)
{
    const Gpu::Vertex vertices[] = {
        { -2, 1, -1, 0, 0, 1, 0, 0 },
        { 4, -3, 5, 0, 0, 1, 0, 0 },
        { 0, 7, 2, 0, 0, 1, 0, 0 },
    };
    const auto box = Gpu::bounds(vertices, 3);
    EXPECT_FLOAT_EQ(box.center.x, 1.0f);
    EXPECT_FLOAT_EQ(box.center.y, 2.0f);
    EXPECT_FLOAT_EQ(box.center.z, 2.0f);
    EXPECT_FLOAT_EQ(box.halfExtent.x, 3.0f);
    EXPECT_FLOAT_EQ(box.halfExtent.y, 5.0f);
    EXPECT_FLOAT_EQ(box.halfExtent.z, 3.0f);
}

TEST(FilamentRenderer, EmptyGeometryHasNonzeroBounds)
{
    const auto box = Gpu::bounds(nullptr, 0);
    EXPECT_FLOAT_EQ(box.center.x, 0.0f);
    EXPECT_GT(box.halfExtent.x, 0.0f);
    EXPECT_GT(box.halfExtent.y, 0.0f);
    EXPECT_GT(box.halfExtent.z, 0.0f);
}

TEST(FilamentRenderer, ModelBudgetUsesPackedVerticesAndWideIndices)
{
    CpuMesh mesh;
    mesh.primitives.resize(1);
    mesh.primitives[0].vertices.resize(3);
    mesh.primitives[0].indices = { 0, 1, 2 };
    mesh.images.resize(1);
    mesh.images[0].rgba.resize(8);
    EXPECT_EQ(Gpu::vertexStride(), 48u);
    EXPECT_EQ(meshDrawGpuBytes(mesh), 3u * Gpu::vertexStride() + 3u * sizeof(uint32_t) + 8u);
    EXPECT_EQ(slabVertexIndex(65530, 4), 65534u);
    EXPECT_EQ(meshDrawGpuBytes({}), 0u);
}

TEST(FilamentRenderer, ResourceCallsAreSafeWithoutADevice)
{
    ASSERT_FALSE(Gpu::ready());
    EXPECT_EQ(Gpu::createTexture(1, 1).handle, nullptr);
    EXPECT_EQ(Gpu::createVertices(3), nullptr);
    EXPECT_EQ(Gpu::createIndices(3), nullptr);
    EXPECT_EQ(Gpu::createMaterial(Gpu::MaterialKind::opaque), nullptr);
    EXPECT_FALSE(meshDrawInit());
    EXPECT_EQ(meshDrawUploadModel({}).status, MeshUploadStatus::capacity);
    Gpu::Geometry geometry;
    Gpu::Texture texture;
    Gpu::destroy(geometry);
    Gpu::destroy(texture);
    Gpu::beginScene();
    uint8_t pixels[4]{};
    EXPECT_FALSE(Gpu::render(1, 1, pixels));
    meshDrawShutdown();
    Gpu::shutdown();
}

TEST(FilamentRenderer, NoopBackendUploadsAndReleasesSharedGeometry)
{
    struct Cleanup
    {
        ~Cleanup()
        {
            meshDrawShutdown();
            Gpu::shutdown();
        }
    } cleanup;
    ASSERT_TRUE(Gpu::initialize(true));
    CpuMesh mesh;
    mesh.primitives.resize(1);
    mesh.primitives[0].vertices = {
        { 0, 0, 0, 0, 0, 1, 0, 0 },
        { 1, 0, 0, 0, 0, 1, 1, 0 },
        { 0, 1, 0, 0, 0, 1, 0, 1 },
    };
    mesh.primitives[0].indices = { 0, 1, 2 };
    const auto first = meshDrawUploadModel(mesh);
    const auto second = meshDrawUploadModel(mesh);
    ASSERT_EQ(first.status, MeshUploadStatus::ready);
    ASSERT_EQ(second.status, MeshUploadStatus::ready);
    const MeshInstance instances[] = { { 0, 0, 0, 0 }, { 3, 4, 5, 1.0f } };
    Gpu::beginScene();
    meshDrawBeginFrame();
    meshDrawModel(first.id, instances, 2);
    meshDrawModel(second.id, instances, 2);
    Gpu::flush();
    Gpu::beginScene();
    meshDrawReleaseModel(first.id);
    const auto recycled = meshDrawUploadModel(mesh);
    ASSERT_EQ(recycled.status, MeshUploadStatus::ready);
    EXPECT_EQ(recycled.id, first.id);
    meshDrawModel(recycled.id, instances, 1);
    Gpu::flush();
}
