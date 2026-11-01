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
#include "openrct2-3d/src/SpriteQuad.hpp"
#include "openrct2-3d/src/ViewerScale.hpp"

#include <cmath>
#include <fstream>
#include <gtest/gtest.h>
#include <openrct2/core/File.h>
#include <openrct2/core/Path.hpp>
#include <string>
#include <vector>

using namespace OpenRCT2::View3D;

namespace
{
    // The same bytes as data/object-models/rct2.scenery_small.tcrp.glb, copied into testdata
    // so CMake's testdata link and the Visual Studio xcopy both find it. No path macro.
    std::string tcrpGlbPath()
    {
        return OpenRCT2::Path::Combine(TestData::GetBasePath(), u8"object-models", u8"rct2.scenery_small.tcrp.glb");
    }

    std::vector<uint8_t> readFile(const std::string& path)
    {
        std::ifstream in(path, std::ios::binary);
        EXPECT_TRUE(static_cast<bool>(in)) << path;
        if (!in)
        {
            return {};
        }
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
} // namespace

TEST(ViewerScale, LandStepMatchesParkView)
{
    EXPECT_FLOAT_EQ(kLandStepHeight, 0.5f);
    EXPECT_FLOAT_EQ(viewerZ(16), 0.5f);
    EXPECT_FLOAT_EQ(viewerZ(32), 1.0f);
    EXPECT_FLOAT_EQ(viewerZ(8), 0.25f);
    EXPECT_FLOAT_EQ(kGlbVerticalScale, 1.0f);
}

TEST(ViewerScale, GltfYUpBecomesViewerZUp)
{
    // glTF position min from TCRP.glb, before the axis swap.
    const Vec3 view = gltfToViewer(-0.81184787f, -0.09375f, -0.69023681f);
    EXPECT_NEAR(view.x, -0.81184787f, 1.0e-5f);
    EXPECT_NEAR(view.y, 0.69023681f, 1.0e-5f);
    EXPECT_NEAR(view.z, -0.09375f, 1.0e-5f);

    const Vec3 tip = gltfToViewer(0.0f, 4.1875f, 0.0f);
    EXPECT_NEAR(tip.z, 4.1875f * kGlbVerticalScale, 1.0e-5f);
}

TEST(ViewerScale, ModelYawMatchesTileRotation)
{
    // Direction 1 is CoordsXY::rotate case 1: (x, y) -> (y, -x).
    float x = 0.0f;
    float y = 0.0f;
    rotateYaw(modelYawRadians(1), 1.0f, 0.0f, x, y);
    EXPECT_NEAR(x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(y, -1.0f, 1.0e-5f);

    rotateYaw(modelYawRadians(0), 0.4f, -0.2f, x, y);
    EXPECT_NEAR(x, 0.4f, 1.0e-5f);
    EXPECT_NEAR(y, -0.2f, 1.0e-5f);
}

TEST(SpriteQuad, FacesTheFourIsometricCameras)
{
    const SpriteQuad face0 = makeDirectionSpriteQuad(0, 32, 64, -16, -64);
    const SpriteQuad face1 = makeDirectionSpriteQuad(1, 32, 64, -16, -64);
    const SpriteQuad face2 = makeDirectionSpriteQuad(2, 32, 64, -16, -64);

    EXPECT_GT(face0.normal.x, 0.5f);
    EXPECT_GT(face0.normal.y, 0.5f);
    EXPECT_NEAR(face0.normal.x * face1.normal.x + face0.normal.y * face1.normal.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(face0.normal.x + face2.normal.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(face0.normal.y + face2.normal.y, 0.0f, 1.0e-5f);

    // yOffset -64 puts the bottom of a 64px sprite on the anchor.
    EXPECT_NEAR(face0.corner[0].z, 0.0f, 1.0e-5f);
    EXPECT_NEAR(face0.corner[2].z, 64.0f * kViewerZPerSimUnit, 1.0e-4f);

    const float width = std::hypot(face0.corner[1].x - face0.corner[0].x, face0.corner[1].y - face0.corner[0].y);
    EXPECT_NEAR(width, 32.0f * kSpritePixelAcross, 1.0e-4f);
}

TEST(GlbMesh, CorsicanPineSitsOnTheTile)
{
    const std::string path = tcrpGlbPath();
    ASSERT_TRUE(OpenRCT2::File::Exists(path)) << path;
    const std::vector<uint8_t> bytes = readFile(path);
    ASSERT_FALSE(bytes.empty());

    std::string error;
    const std::optional<CpuMesh> mesh = loadGlbMesh(bytes.data(), bytes.size(), {}, error);
    ASSERT_TRUE(mesh.has_value()) << error;
    ASSERT_EQ(mesh->primitives.size(), 1u);
    EXPECT_EQ(mesh->primitives[0].vertices.size(), 1908u);
    EXPECT_EQ(mesh->primitives[0].indices.size(), 2592u);
    EXPECT_EQ(mesh->primitives[0].alpha, MeshAlpha::mask);
    EXPECT_NEAR(mesh->primitives[0].alphaCutoff, 0.5f, 1.0e-6f);
    EXPECT_TRUE(mesh->primitives[0].doubleSided);

    ASSERT_EQ(mesh->images.size(), 1u);
    EXPECT_TRUE(mesh->images[0].nearest);
    EXPECT_TRUE(mesh->images[0].clamp);
    ASSERT_GE(mesh->images[0].png.size(), 8u);
    EXPECT_EQ(mesh->images[0].png[0], 0x89);
    EXPECT_EQ(mesh->images[0].png[1], 'P');

    // Converted bounds. Z is the tree, slightly sunk at the base. A missing axis
    // swap would leave Z in the glTF ground range, about -0.69 to 0.73.
    EXPECT_NEAR(mesh->bounds.minX, -0.81184787f, 1.0e-4f);
    EXPECT_NEAR(mesh->bounds.maxX, 0.67550898f, 1.0e-4f);
    EXPECT_NEAR(mesh->bounds.minY, -0.72916222f, 1.0e-4f);
    EXPECT_NEAR(mesh->bounds.maxY, 0.69023681f, 1.0e-4f);
    EXPECT_NEAR(mesh->bounds.minZ, -0.09375f * kGlbVerticalScale, 1.0e-4f);
    EXPECT_NEAR(mesh->bounds.maxZ, 4.1875f * kGlbVerticalScale, 1.0e-4f);

    const MeshVertex& vertex = mesh->primitives[0].vertices[0];
    const float length = std::sqrt(vertex.nx * vertex.nx + vertex.ny * vertex.ny + vertex.nz * vertex.nz);
    EXPECT_NEAR(length, 1.0f, 1.0e-3f);
}

TEST(GlbMesh, RejectsEmptyInput)
{
    std::string error;
    const std::optional<CpuMesh> mesh = loadGlbMesh(nullptr, 0, {}, error);
    EXPECT_FALSE(mesh.has_value());
    EXPECT_FALSE(error.empty());
}
