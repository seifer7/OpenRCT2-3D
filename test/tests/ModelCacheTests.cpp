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

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include <openrct2/core/File.h>
#include <openrct2/core/Path.hpp>
#include <openrct2/core/String.hpp>
#include <random>
#include <string>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <process.h>
    #include <windows.h>
    #undef CreateDirectory
    #undef CreateWindow
    #undef GetMessage
#else
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

#ifdef __linux__
    #include <fcntl.h>
#endif

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

    std::string temporaryDirectory()
    {
#ifdef _WIN32
        wchar_t buffer[MAX_PATH + 1] = {};
        const DWORD length = GetTempPathW(MAX_PATH, buffer);
        if (length == 0 || length > MAX_PATH)
        {
            return ".";
        }
        std::string path = OpenRCT2::String::toUtf8(std::wstring_view(buffer, length));
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

    class CacheDir
    {
    public:
        std::string root;
        std::string source;
        std::string cache;

        CacheDir()
        {
            std::random_device device;
            const uint64_t nonce = (static_cast<uint64_t>(device()) << 32) ^ device();
#ifdef _WIN32
            const int pid = _getpid();
#else
            const int pid = getpid();
#endif
            root = OpenRCT2::Path::Combine(
                temporaryDirectory(), "orct2-model-cache-" + std::to_string(pid) + "-" + std::to_string(nonce));
            source = OpenRCT2::Path::Combine(root, "object-models");
            cache = OpenRCT2::Path::Combine(root, "cache");
            OpenRCT2::Path::CreateDirectory(source);
            OpenRCT2::Path::CreateDirectory(cache);
        }

        ~CacheDir()
        {
            OpenRCT2::Path::DeleteDirectory(root);
        }

        CacheDir(const CacheDir&) = delete;
        CacheDir& operator=(const CacheDir&) = delete;

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

static uint64_t readU64(const std::vector<uint8_t>& bytes, size_t offset);
static bool setModifiedNs(const std::string& path, uint64_t ns);

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
    resetCacheChecksumPassCount();
    const ModelCacheStats second = prepareModelCache(sources, dir.cache, nullptr);
    EXPECT_EQ(second.converted, 0);
    EXPECT_EQ(second.skipped, 1);
    EXPECT_EQ(second.failed, 0);
    EXPECT_EQ(cacheBytes(dir, "pine"), before);
    EXPECT_EQ(cacheChecksumPassCount(), 0u);

    dir.writeGlb("pine.glb", triangleGlb(2.0f));
    // A rewrite in the same nanosecond keeps the fast path. Move the stamp so this checks a real change.
    ASSERT_TRUE(setModifiedNs(
        OpenRCT2::Path::Combine(dir.source, "pine.glb"), readU64(before, kModelCacheMtimeOffset) + 1000000000uLL));
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

    std::string error = "stale";
    const std::optional<CpuMesh> mesh = loadModelForDraw("broken", sources, dir.cache, error);
    EXPECT_FALSE(mesh.has_value());
    EXPECT_TRUE(error.empty());
    EXPECT_FALSE(modelIsValidated("broken"));
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

TEST(ModelCache, StartupRebuildsGarbageCache)
{
    CacheDir dir;
    dir.writeGlb("pine.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    ASSERT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);

    const std::string cacheFile = OpenRCT2::Path::Combine(dir.cache, "pine.o3d");
    const std::string garbage = "not a cache";
    OpenRCT2::File::WriteAllBytes(cacheFile, garbage.data(), garbage.size());

    const ModelCacheStats again = prepareModelCache(sources, dir.cache, nullptr);
    EXPECT_EQ(again.converted, 1);
    EXPECT_TRUE(modelIsValidated("pine"));
    std::string error;
    const std::optional<CpuMesh> mesh = loadModelForDraw("pine", sources, dir.cache, error);
    ASSERT_TRUE(mesh.has_value()) << error;
    EXPECT_NEAR(mesh->primitives[0].vertices[0].z, 1.0f * kGlbVerticalScale, 1.0e-4f);
}

static uint32_t readU32(const std::vector<uint8_t>& bytes, size_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<uint32_t>(bytes[offset + 2]) << 16) | (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

static void writeU32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24);
}

static uint64_t readU64(const std::vector<uint8_t>& bytes, size_t offset)
{
    return static_cast<uint64_t>(readU32(bytes, offset)) | (static_cast<uint64_t>(readU32(bytes, offset + 4)) << 32);
}

static void writeU64(std::vector<uint8_t>& bytes, size_t offset, uint64_t value)
{
    writeU32(bytes, offset, static_cast<uint32_t>(value));
    writeU32(bytes, offset + 4, static_cast<uint32_t>(value >> 32));
}

static bool setModifiedNs(const std::string& path, uint64_t ns)
{
#ifdef _WIN32
    const std::wstring wide = OpenRCT2::String::toWideChar(path);
    const HANDLE file = CreateFileW(
        wide.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    const uint64_t ticks = 116444736000000000uLL + ns / 100uLL;
    FILETIME written{};
    written.dwLowDateTime = static_cast<DWORD>(ticks);
    written.dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    const BOOL ok = SetFileTime(file, nullptr, nullptr, &written);
    CloseHandle(file);
    return ok != 0;
#else
    const uint64_t sec = ns / 1000000000uLL;
    const uint64_t nsec = ns % 1000000000uLL;
    timespec times[2]{};
    times[0].tv_sec = times[1].tv_sec = static_cast<time_t>(sec);
    times[0].tv_nsec = times[1].tv_nsec = static_cast<long>(nsec);
    return utimensat(AT_FDCWD, path.c_str(), times, 0) == 0;
#endif
}

// First vertex Z sits 28 bytes into the body: two counts, the flag word, cutoff, image index, then x and y.
constexpr size_t kFirstVertexZ = kModelCacheHeaderBytes + 28;

TEST(ModelCache, MatchingStampTrustsAShortCache)
{
    CacheDir dir;
    dir.writeGlb("pine.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    ASSERT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);

    std::vector<uint8_t> bytes = cacheBytes(dir, "pine");
    ASSERT_GT(bytes.size(), kModelCacheHeaderBytes + 40);
    bytes.resize(kModelCacheHeaderBytes + 40);
    const std::string cacheFile = OpenRCT2::Path::Combine(dir.cache, "pine.o3d");
    OpenRCT2::File::WriteAllBytes(cacheFile, bytes.data(), bytes.size());

    resetCacheChecksumPassCount();
    const ModelCacheStats again = prepareModelCache(sources, dir.cache, nullptr);
    EXPECT_EQ(again.skipped, 1);
    EXPECT_EQ(again.converted, 0);
    EXPECT_EQ(cacheChecksumPassCount(), 0u);
    EXPECT_TRUE(modelIsValidated("pine"));
    std::string error;
    EXPECT_FALSE(loadModelForDraw("pine", sources, dir.cache, error).has_value());
    EXPECT_FALSE(error.empty());
}

TEST(ModelCache, MatchingStampTrustsACorruptBody)
{
    CacheDir dir;
    dir.writeGlb("pine.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    ASSERT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);

    std::vector<uint8_t> bytes = cacheBytes(dir, "pine");
    ASSERT_GT(bytes.size(), kFirstVertexZ + 4);
    bytes[kFirstVertexZ] ^= 0xff;
    const std::string cacheFile = OpenRCT2::Path::Combine(dir.cache, "pine.o3d");
    OpenRCT2::File::WriteAllBytes(cacheFile, bytes.data(), bytes.size());

    resetCacheChecksumPassCount();
    EXPECT_EQ(prepareModelCache(sources, dir.cache, nullptr).skipped, 1);
    EXPECT_EQ(cacheChecksumPassCount(), 0u);
    std::string error;
    const std::optional<CpuMesh> mesh = loadModelForDraw("pine", sources, dir.cache, error);
    ASSERT_TRUE(mesh.has_value()) << error;
    EXPECT_NE(mesh->primitives[0].vertices[0].z, 1.0f * kGlbVerticalScale);
}

TEST(ModelCache, ConverterVersionAndLimitsRebuild)
{
    CacheDir dir;
    dir.writeGlb("pine.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    ASSERT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);
    const std::string cacheFile = OpenRCT2::Path::Combine(dir.cache, "pine.o3d");

    std::vector<uint8_t> bytes = cacheBytes(dir, "pine");
    ASSERT_GE(bytes.size(), kModelCacheHeaderBytes);
    writeU32(bytes, kModelCacheConverterOffset, 99);
    writeU64(bytes, kModelCacheChecksumOffset, cacheFileChecksum(bytes.data(), bytes.size()));
    OpenRCT2::File::WriteAllBytes(cacheFile, bytes.data(), bytes.size());

    EXPECT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);
    EXPECT_TRUE(modelIsValidated("pine"));
    const std::vector<uint8_t> rebuilt = cacheBytes(dir, "pine");
    EXPECT_EQ(readU32(rebuilt, kModelCacheConverterOffset), kGlbConverterVersion);

    bytes = rebuilt;
    writeU32(bytes, kModelCacheMaxVerticesOffset, 1);
    writeU64(bytes, kModelCacheChecksumOffset, cacheFileChecksum(bytes.data(), bytes.size()));
    OpenRCT2::File::WriteAllBytes(cacheFile, bytes.data(), bytes.size());
    EXPECT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);
    EXPECT_EQ(readU32(cacheBytes(dir, "pine"), kModelCacheMaxVerticesOffset), 65535u);
}

TEST(ModelCache, RejectsUnsafeIdentifiers)
{
    EXPECT_TRUE(identifierIsSafe("rct2.scenery_small.tcrp"));
    EXPECT_TRUE(identifierIsSafe("rct2.scenery_small.trf2"));
    EXPECT_TRUE(identifierIsSafe("openrct2.ride.hybrid_coaster"));
    EXPECT_TRUE(identifierIsSafe("tygrysek90.station.abstract-open"));
    // JSON ids keep authored case, so a file named MyTree.glb is a real object id.
    EXPECT_TRUE(identifierIsSafe("MyTree"));
    EXPECT_TRUE(identifierIsSafe("COM0"));
    EXPECT_TRUE(identifierIsSafe("com0"));

    const char* names[] = { "CON",     "con",    "PRN",    "AUX",   "NUL",  "COM1", "com9",     "LPT1", "lpt9",
                            "con.txt", "CONIN$", "bad:id", "a/b",   "a\\b", "a<b",  "a>b",      "a\"b", "a|b",
                            "a?b",     "a*b",    "trail.", ".lead", "a ",   "",     "has space" };
    for (const char* name : names)
    {
        EXPECT_FALSE(identifierIsSafe(name)) << name;
    }
}

TEST(ModelCache, DirectoriesAreUnique)
{
    CacheDir first;
    CacheDir second;
    EXPECT_NE(first.root, second.root);
}

TEST(ModelCache, UpperCaseIdentifierIsConverted)
{
    CacheDir dir;
    dir.writeGlb("MyTree.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    const ModelCacheStats stats = prepareModelCache(sources, dir.cache, nullptr);
    EXPECT_EQ(stats.converted, 1);
    EXPECT_EQ(stats.failed, 0);
    std::string error;
    const std::optional<CpuMesh> mesh = loadModelForDraw("MyTree", sources, dir.cache, error);
    ASSERT_TRUE(mesh.has_value()) << error;
    EXPECT_NEAR(mesh->primitives[0].vertices[0].z, 1.0f * kGlbVerticalScale, 1.0e-4f);
    EXPECT_TRUE(OpenRCT2::File::Exists(OpenRCT2::Path::Combine(dir.cache, "mytree.o3d")));
}

TEST(ModelCache, RejectedNameIsNotCached)
{
    CacheDir dir;
    dir.writeGlb("has space.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    const ModelCacheStats stats = prepareModelCache(sources, dir.cache, nullptr);
    EXPECT_EQ(stats.converted, 0);
    EXPECT_EQ(stats.failed, 0);
    EXPECT_FALSE(OpenRCT2::File::Exists(OpenRCT2::Path::Combine(dir.cache, "has space.o3d")));
}

TEST(ModelCache, StartupRejectsChecksumWhenGlbCannotRebuild)
{
    CacheDir dir;
    dir.writeGlb("pine.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    ASSERT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);

    std::vector<uint8_t> bytes = cacheBytes(dir, "pine");
    ASSERT_GT(bytes.size(), kFirstVertexZ);
    bytes[kFirstVertexZ] ^= 0xff;
    const std::string cacheFile = OpenRCT2::Path::Combine(dir.cache, "pine.o3d");
    OpenRCT2::File::WriteAllBytes(cacheFile, bytes.data(), bytes.size());
    const std::string junk = "this is not a glb";
    dir.writeGlb("pine.glb", std::vector<uint8_t>(junk.begin(), junk.end()));

    const ModelCacheStats stats = prepareModelCache(sources, dir.cache, nullptr);
    EXPECT_EQ(stats.failed, 1);
    EXPECT_EQ(stats.converted, 0);
    EXPECT_FALSE(modelIsValidated("pine"));
    std::string error = "stale";
    EXPECT_FALSE(loadModelForDraw("pine", sources, dir.cache, error).has_value());
    EXPECT_TRUE(error.empty());
}

TEST(ModelCache, InGameLoadDoesNotChecksum)
{
    CacheDir dir;
    dir.writeGlb("pine.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    ASSERT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);
    ASSERT_TRUE(modelIsValidated("pine"));
    resetCacheChecksumPassCount();

    std::string error;
    const std::optional<CpuMesh> mesh = loadModelForDraw("pine", sources, dir.cache, error);
    ASSERT_TRUE(mesh.has_value()) << error;
    EXPECT_EQ(mesh->primitives.size(), 1u);
    EXPECT_EQ(cacheChecksumPassCount(), 0u);

    std::vector<uint8_t> bytes = cacheBytes(dir, "pine");
    ASSERT_GT(bytes.size(), kFirstVertexZ + 4);
    bytes[kFirstVertexZ] ^= 0xff;
    const std::string cacheFile = OpenRCT2::Path::Combine(dir.cache, "pine.o3d");
    OpenRCT2::File::WriteAllBytes(cacheFile, bytes.data(), bytes.size());
    const std::optional<CpuMesh> corrupted = loadModelForDraw("pine", sources, dir.cache, error);
    ASSERT_TRUE(corrupted.has_value()) << error;
    EXPECT_EQ(corrupted->primitives.size(), 1u);
    ASSERT_FALSE(corrupted->primitives[0].vertices.empty());
    EXPECT_EQ(cacheChecksumPassCount(), 0u);
    EXPECT_NE(corrupted->primitives[0].vertices[0].z, 1.0f * kGlbVerticalScale);
}

TEST(ModelCache, InGameHugeCountStaysBounded)
{
    CacheDir dir;
    dir.writeGlb("pine.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    ASSERT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);
    resetCacheChecksumPassCount();

    std::vector<uint8_t> bytes = cacheBytes(dir, "pine");
    writeU32(bytes, kModelCachePrimitiveCountOffset, 1000000u);
    const std::string cacheFile = OpenRCT2::Path::Combine(dir.cache, "pine.o3d");
    OpenRCT2::File::WriteAllBytes(cacheFile, bytes.data(), bytes.size());

    std::string error;
    EXPECT_FALSE(loadModelForDraw("pine", sources, dir.cache, error).has_value());
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(cacheChecksumPassCount(), 0u);
    EXPECT_EQ(readU32(cacheBytes(dir, "pine"), kModelCachePrimitiveCountOffset), 1000000u);
}

TEST(ModelCache, InGameMissingAndShortFileFallBack)
{
    CacheDir dir;
    dir.writeGlb("pine.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    ASSERT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);
    const std::string cacheFile = OpenRCT2::Path::Combine(dir.cache, "pine.o3d");
    OpenRCT2::File::Delete(cacheFile);

    std::string error;
    EXPECT_FALSE(loadModelForDraw("pine", sources, dir.cache, error).has_value());
    EXPECT_FALSE(error.empty());

    const std::string tiny = "short";
    OpenRCT2::File::WriteAllBytes(cacheFile, tiny.data(), tiny.size());
    error.clear();
    EXPECT_FALSE(loadModelForDraw("pine", sources, dir.cache, error).has_value());
    EXPECT_FALSE(error.empty());
}

TEST(ModelCache, UnvalidatedIdFallsBack)
{
    std::string error = "stale";
    EXPECT_FALSE(loadModelForDraw("not-prepared", {}, {}, error).has_value());
    EXPECT_TRUE(error.empty());
    EXPECT_FALSE(modelIsValidated("not-prepared"));
}

TEST(ModelCache, CaseFoldedDuplicateUsesDataDir)
{
    CacheDir data;
    CacheDir user;
    data.writeGlb("MyTree.glb", triangleGlb(1.0f));
    user.writeGlb("mytree.glb", triangleGlb(4.0f));
    const std::vector<std::string> sources = { data.source, user.source };

    const ModelCacheStats first = prepareModelCache(sources, data.cache, nullptr);
    EXPECT_EQ(first.converted, 1);
    EXPECT_EQ(first.failed, 0);
    EXPECT_TRUE(modelIsValidated("MyTree"));
    EXPECT_TRUE(modelIsValidated("mytree"));
    EXPECT_TRUE(OpenRCT2::File::Exists(OpenRCT2::Path::Combine(data.cache, "mytree.o3d")));

    std::string error;
    const std::optional<CpuMesh> mesh = loadModelForDraw("MyTree", sources, data.cache, error);
    ASSERT_TRUE(mesh.has_value()) << error;
    EXPECT_NEAR(mesh->primitives[0].vertices[0].z, 1.0f * kGlbVerticalScale, 1.0e-4f);

    const ModelCacheStats second = prepareModelCache(sources, data.cache, nullptr);
    EXPECT_EQ(second.converted, 0);
    EXPECT_EQ(second.skipped, 1);
}

TEST(ModelCache, StartupChecksumOfTenThousand)
{
    CacheDir dir;
    dir.writeGlb("pine.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    ASSERT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);
    const std::vector<uint8_t> bytes = cacheBytes(dir, "pine");
    constexpr int32_t kCount = 10000;
    std::vector<std::string> paths;
    paths.reserve(kCount);
    for (int32_t i = 0; i < kCount; i++)
    {
        const std::string path = OpenRCT2::Path::Combine(dir.cache, "m" + std::to_string(i) + ".o3d");
        OpenRCT2::File::WriteAllBytes(path, bytes.data(), bytes.size());
        paths.push_back(path);
    }
#ifdef __linux__
    for (const std::string& path : paths)
    {
        const int fd = open(path.c_str(), O_RDONLY);
        if (fd >= 0)
        {
            posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
            close(fd);
        }
    }
    // Optional. The unit-test user usually cannot drop the page cache; a
    // measurement run sets O3D_DROP_CACHE=1 and is allowed to write drop_caches.
    if (const char* drop = std::getenv("O3D_DROP_CACHE"); drop != nullptr && drop[0] == '1')
    {
        sync();
        const int fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
        if (fd >= 0)
        {
            const char three[] = "3\n";
            if (write(fd, three, 2) == 2)
            {
                std::printf("o3d checksum dropped page cache\n");
            }
            close(fd);
        }
    }
#endif
    resetCacheChecksumPassCount();
    const auto coldStart = std::chrono::steady_clock::now();
    for (const std::string& path : paths)
    {
        ASSERT_TRUE(cacheChecksumMatchesFile(path));
    }
    const auto coldUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - coldStart)
                            .count();
    const auto warmStart = std::chrono::steady_clock::now();
    for (const std::string& path : paths)
    {
        ASSERT_TRUE(cacheChecksumMatchesFile(path));
    }
    const auto warmUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - warmStart)
                            .count();
    EXPECT_EQ(cacheChecksumPassCount(), static_cast<uint64_t>(kCount) * 2u);
    std::printf(
        "o3d checksum %d files bytes=%zu cold_us=%lld warm_us=%lld per_cold_us=%.3f per_warm_us=%.3f\n", kCount, bytes.size(),
        static_cast<long long>(coldUs), static_cast<long long>(warmUs), static_cast<double>(coldUs) / kCount,
        static_cast<double>(warmUs) / kCount);
}

static bool filesystemKeepsSubSecondMtime()
{
    CacheDir dir;
    dir.writeGlb("probe.glb", triangleGlb(1.0f));
    const std::string path = OpenRCT2::Path::Combine(dir.source, "probe.glb");
    const uint64_t base = cacheSourceModifiedNs(path);
    const uint64_t want = (base / 1000000000uLL) * 1000000000uLL + 111111111uLL;
    if (!setModifiedNs(path, want))
    {
        return false;
    }
    return cacheSourceModifiedNs(path) == want;
}

TEST(ModelCache, MtimeOnlyChangeRefreshesStamp)
{
    CacheDir dir;
    dir.writeGlb("pine.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    ASSERT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);

    const std::vector<uint8_t> before = cacheBytes(dir, "pine");
    const uint64_t stored = readU64(before, kModelCacheMtimeOffset);
    const uint64_t bumped = stored + 5000000000uLL;
    const std::string glbPath = OpenRCT2::Path::Combine(dir.source, "pine.glb");
    ASSERT_TRUE(setModifiedNs(glbPath, bumped));
    ASSERT_EQ(cacheSourceModifiedNs(glbPath) / 1000000000uLL, bumped / 1000000000uLL);

    const ModelCacheStats stats = prepareModelCache(sources, dir.cache, nullptr);
    EXPECT_EQ(stats.converted, 0);
    EXPECT_EQ(stats.skipped, 1);
    EXPECT_EQ(stats.failed, 0);

    const std::vector<uint8_t> after = cacheBytes(dir, "pine");
    EXPECT_EQ(readU64(after, kModelCacheMtimeOffset), cacheSourceModifiedNs(glbPath));
    EXPECT_NE(readU64(after, kModelCacheMtimeOffset), stored);
    ASSERT_GT(before.size(), kModelCacheHeaderBytes);
    EXPECT_EQ(
        std::vector<uint8_t>(after.begin() + kModelCacheHeaderBytes, after.end()),
        std::vector<uint8_t>(before.begin() + kModelCacheHeaderBytes, before.end()));
}

TEST(ModelCache, SameSecondEditIsNotStale)
{
    if (!filesystemKeepsSubSecondMtime())
    {
        GTEST_SKIP() << "filesystem rounds modification times to whole seconds";
    }
    CacheDir dir;
    dir.writeGlb("pine.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    ASSERT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);

    const uint64_t stored = readU64(cacheBytes(dir, "pine"), kModelCacheMtimeOffset);
    const uint64_t second = stored / 1000000000uLL;
    const uint64_t nsec = stored % 1000000000uLL;
    const uint64_t otherNsec = nsec < 500000000uLL ? nsec + 500000000uLL : nsec - 500000000uLL;
    const std::string glbPath = OpenRCT2::Path::Combine(dir.source, "pine.glb");
    dir.writeGlb("pine.glb", triangleGlb(2.0f));
    ASSERT_EQ(triangleGlb(1.0f).size(), triangleGlb(2.0f).size());
    ASSERT_TRUE(setModifiedNs(glbPath, second * 1000000000uLL + otherNsec));
    const uint64_t updated = cacheSourceModifiedNs(glbPath);
    ASSERT_EQ(updated / 1000000000uLL, second);
    ASSERT_NE(updated, stored);

    const ModelCacheStats stats = prepareModelCache(sources, dir.cache, nullptr);
    EXPECT_EQ(stats.converted, 1);
    EXPECT_EQ(stats.skipped, 0);
    std::string error;
    const std::optional<CpuMesh> mesh = loadModelForDraw("pine", sources, dir.cache, error);
    ASSERT_TRUE(mesh.has_value()) << error;
    EXPECT_NEAR(mesh->primitives[0].vertices[0].z, 2.0f * kGlbVerticalScale, 1.0e-4f);
}
