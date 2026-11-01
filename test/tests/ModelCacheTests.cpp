/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "openrct2-3d/src/ModelCache.hpp"
#include "openrct2-3d/src/ViewerScale.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#include <openrct2/core/File.h>
#include <openrct2/core/Path.hpp>
#include <string>
#include <vector>

using namespace OpenRCT2::View3D;

namespace
{
    void appendU32(std::vector<uint8_t>& out, uint32_t value)
    {
        out.push_back(static_cast<uint8_t>(value));
        out.push_back(static_cast<uint8_t>(value >> 8));
        out.push_back(static_cast<uint8_t>(value >> 16));
        out.push_back(static_cast<uint8_t>(value >> 24));
    }

    void appendF32(std::vector<uint8_t>& out, float value)
    {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        appendU32(out, bits);
    }

    // One triangle. glTF Y is up, so the first vertex's Y becomes viewer Z.
    std::vector<uint8_t> triangleGlb(float y0)
    {
        const std::string json = std::string("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":36}],")
            + "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36}],"
            + "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
            + "\"min\":[0,0,0],\"max\":[1,2,1]}]," + "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
            + "\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";
        std::vector<uint8_t> jsonBytes(json.begin(), json.end());
        while (jsonBytes.size() % 4 != 0)
        {
            jsonBytes.push_back(' ');
        }

        std::vector<uint8_t> bin;
        const float positions[9] = { 0.0f, y0, 0.0f, 1.0f, y0, 0.0f, 0.0f, y0, 1.0f };
        for (float value : positions)
        {
            appendF32(bin, value);
        }

        std::vector<uint8_t> glb;
        const uint32_t length = 12u + 8u + static_cast<uint32_t>(jsonBytes.size()) + 8u + static_cast<uint32_t>(bin.size());
        appendU32(glb, 0x46546C67u);
        appendU32(glb, 2u);
        appendU32(glb, length);
        appendU32(glb, static_cast<uint32_t>(jsonBytes.size()));
        appendU32(glb, 0x4E4F534Au);
        glb.insert(glb.end(), jsonBytes.begin(), jsonBytes.end());
        appendU32(glb, static_cast<uint32_t>(bin.size()));
        appendU32(glb, 0x004E4942u);
        glb.insert(glb.end(), bin.begin(), bin.end());
        return glb;
    }

    class CacheDir
    {
    public:
        std::string source;
        std::string cache;

        CacheDir()
        {
            static int32_t next = 0;
            const auto root = std::filesystem::temp_directory_path() / ("orct2-model-cache-test-" + std::to_string(next++));
            std::filesystem::remove_all(root);
            source = (root / "object-models").string();
            cache = (root / "cache").string();
            std::filesystem::create_directories(source);
            std::filesystem::create_directories(cache);
        }

        void writeGlb(const std::string& name, const std::vector<uint8_t>& bytes) const
        {
            OpenRCT2::File::WriteAllBytes(OpenRCT2::Path::Combine(source, name), bytes.data(), bytes.size());
        }
    };

    std::vector<uint8_t> cacheBytes(const CacheDir& dir, const std::string& identifier)
    {
        return OpenRCT2::File::ReadAllBytes(OpenRCT2::Path::Combine(dir.cache, identifier + ".o3d"));
    }
} // namespace

TEST(ModelCache, ConvertsSkipsAndRebuilds)
{
    CacheDir dir;
    dir.writeGlb("pine.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };

    const ModelCacheStats first = prepareModelCache(sources, dir.cache, nullptr);
    EXPECT_EQ(first.converted, 1);
    EXPECT_EQ(first.skipped, 0);
    EXPECT_EQ(first.failed, 0);

    std::string error;
    const std::optional<CpuMesh> loaded = loadModelForDraw("pine", sources, dir.cache, error);
    ASSERT_TRUE(loaded.has_value()) << error;
    ASSERT_FALSE(loaded->primitives.empty());
    ASSERT_FALSE(loaded->primitives[0].vertices.empty());
    EXPECT_NEAR(loaded->primitives[0].vertices[0].z, 1.0f * kGlbVerticalScale, 1.0e-4f);

    const std::vector<uint8_t> before = cacheBytes(dir, "pine");
    const ModelCacheStats second = prepareModelCache(sources, dir.cache, nullptr);
    EXPECT_EQ(second.converted, 0);
    EXPECT_EQ(second.skipped, 1);
    EXPECT_EQ(second.failed, 0);
    EXPECT_EQ(cacheBytes(dir, "pine"), before);

    dir.writeGlb("pine.glb", triangleGlb(2.0f));
    const ModelCacheStats third = prepareModelCache(sources, dir.cache, nullptr);
    EXPECT_EQ(third.converted, 1);
    EXPECT_EQ(third.skipped, 0);
    EXPECT_EQ(third.failed, 0);

    const std::optional<CpuMesh> rebuilt = loadModelForDraw("pine", sources, dir.cache, error);
    ASSERT_TRUE(rebuilt.has_value()) << error;
    EXPECT_NEAR(rebuilt->primitives[0].vertices[0].z, 2.0f * kGlbVerticalScale, 1.0e-4f);
    EXPECT_NE(cacheBytes(dir, "pine"), before);
}

TEST(ModelCache, BadFileDoesNotCrashOrCache)
{
    CacheDir dir;
    const std::string junk = "this is not a glb";
    dir.writeGlb("broken.glb", std::vector<uint8_t>(junk.begin(), junk.end()));
    const std::vector<std::string> sources = { dir.source };

    const ModelCacheStats stats = prepareModelCache(sources, dir.cache, nullptr);
    EXPECT_EQ(stats.converted, 0);
    EXPECT_EQ(stats.skipped, 0);
    EXPECT_EQ(stats.failed, 1);
    EXPECT_FALSE(OpenRCT2::File::Exists(OpenRCT2::Path::Combine(dir.cache, "broken.o3d")));

    std::string error;
    const std::optional<CpuMesh> mesh = loadModelForDraw("broken", sources, dir.cache, error);
    EXPECT_FALSE(mesh.has_value());
    EXPECT_FALSE(error.empty());
}

TEST(ModelCache, DataDirWinsOverUserDir)
{
    CacheDir data;
    CacheDir user;
    data.writeGlb("shared.glb", triangleGlb(1.0f));
    user.writeGlb("shared.glb", triangleGlb(4.0f));
    const std::vector<std::string> sources = { data.source, user.source };

    const ModelCacheStats stats = prepareModelCache(sources, data.cache, nullptr);
    EXPECT_EQ(stats.converted, 1);

    std::string error;
    const std::optional<CpuMesh> mesh = loadModelForDraw("shared", sources, data.cache, error);
    ASSERT_TRUE(mesh.has_value()) << error;
    EXPECT_NEAR(mesh->primitives[0].vertices[0].z, 1.0f * kGlbVerticalScale, 1.0e-4f);
}

TEST(ModelCache, BadCacheFallsBackToGlb)
{
    CacheDir dir;
    dir.writeGlb("pine.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    ASSERT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);

    const std::string cacheFile = OpenRCT2::Path::Combine(dir.cache, "pine.o3d");
    const std::string garbage = "not a cache";
    OpenRCT2::File::WriteAllBytes(cacheFile, garbage.data(), garbage.size());

    std::string error;
    const std::optional<CpuMesh> mesh = loadModelForDraw("pine", sources, dir.cache, error);
    ASSERT_TRUE(mesh.has_value()) << error;
    EXPECT_NEAR(mesh->primitives[0].vertices[0].z, 1.0f * kGlbVerticalScale, 1.0e-4f);
    EXPECT_TRUE(error.empty());
}
