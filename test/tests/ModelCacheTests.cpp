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

#include <algorithm>
#include <cstdint>
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
    #include <sys/stat.h>
    #include <sys/time.h>
    #include <unistd.h>
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
        char buffer[MAX_PATH + 1] = {};
        const DWORD length = GetTempPathA(MAX_PATH, buffer);
        if (length == 0 || length > MAX_PATH)
        {
            return ".";
        }
        std::string path(buffer, length);
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
    const ModelCacheStats second = prepareModelCache(sources, dir.cache, nullptr);
    EXPECT_EQ(second.converted, 0);
    EXPECT_EQ(second.skipped, 1);
    EXPECT_EQ(second.failed, 0);
    EXPECT_EQ(cacheBytes(dir, "pine"), before);

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

// FNV-1a of every byte except the 8-byte checksum field. Must match ModelCache.cpp.
static uint64_t cacheChecksum(const std::vector<uint8_t>& bytes)
{
    uint64_t hash = 14695981039346656037uLL;
    const auto feed = [&](size_t begin, size_t end) {
        for (size_t i = begin; i < end; i++)
        {
            hash ^= bytes[i];
            hash *= 1099511628211uLL;
        }
    };
    feed(0, std::min(bytes.size(), kModelCacheChecksumOffset));
    if (bytes.size() > kModelCacheChecksumOffset + 8)
    {
        feed(kModelCacheChecksumOffset + 8, bytes.size());
    }
    return hash;
}

static uint64_t fileModifiedNs(const std::string& path)
{
#ifdef _WIN32
    const std::wstring wide = OpenRCT2::String::toWideChar(path);
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetFileAttributesExW(wide.c_str(), GetFileExInfoStandard, &info) == 0)
    {
        return 0;
    }
    const uint64_t ticks = (static_cast<uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32uLL)
        | static_cast<uint64_t>(info.ftLastWriteTime.dwLowDateTime);
    constexpr uint64_t kUnixEpoch = 116444736000000000uLL;
    if (ticks < kUnixEpoch)
    {
        return 0;
    }
    return (ticks - kUnixEpoch) * 100uLL;
#else
    struct stat info{};
    if (stat(path.c_str(), &info) != 0)
    {
        return 0;
    }
    #if defined(__APPLE__)
    if (info.st_mtimespec.tv_sec < 0)
    {
        return 0;
    }
    return static_cast<uint64_t>(info.st_mtimespec.tv_sec) * 1000000000uLL + static_cast<uint64_t>(info.st_mtimespec.tv_nsec);
    #else
    if (info.st_mtim.tv_sec < 0)
    {
        return 0;
    }
    return static_cast<uint64_t>(info.st_mtim.tv_sec) * 1000000000uLL + static_cast<uint64_t>(info.st_mtim.tv_nsec);
    #endif
#endif
}

static bool setModifiedNs(const std::string& path, uint64_t ns)
{
    const uint64_t sec = ns / 1000000000uLL;
    const uint64_t nsec = ns % 1000000000uLL;
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
    timeval times[2]{};
    times[0].tv_sec = times[1].tv_sec = static_cast<time_t>(sec);
    times[0].tv_usec = times[1].tv_usec = static_cast<suseconds_t>(nsec / 1000uLL);
    return utimes(path.c_str(), times) == 0;
#endif
}

// First vertex Z sits 28 bytes into the body: two counts, the flag word, cutoff, image index, then x and y.
constexpr size_t kFirstVertexZ = kModelCacheHeaderBytes + 28;

TEST(ModelCache, TruncatedBodyFallsBackToGlb)
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

    std::string error;
    const std::optional<CpuMesh> mesh = loadModelForDraw("pine", sources, dir.cache, error);
    ASSERT_TRUE(mesh.has_value()) << error;
    EXPECT_NEAR(mesh->primitives[0].vertices[0].z, 1.0f * kGlbVerticalScale, 1.0e-4f);
}

TEST(ModelCache, CorruptBodyFallsBackToGlb)
{
    CacheDir dir;
    dir.writeGlb("pine.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    ASSERT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);

    std::vector<uint8_t> bytes = cacheBytes(dir, "pine");
    ASSERT_GT(bytes.size(), kFirstVertexZ + 4);
    bytes[kFirstVertexZ] ^= 0xff;
    float corruptedZ = 0.0f;
    std::memcpy(&corruptedZ, bytes.data() + kFirstVertexZ, sizeof(corruptedZ));
    EXPECT_NE(corruptedZ, 1.0f * kGlbVerticalScale);
    const std::string cacheFile = OpenRCT2::Path::Combine(dir.cache, "pine.o3d");
    OpenRCT2::File::WriteAllBytes(cacheFile, bytes.data(), bytes.size());

    std::string error;
    const std::optional<CpuMesh> mesh = loadModelForDraw("pine", sources, dir.cache, error);
    ASSERT_TRUE(mesh.has_value()) << error;
    EXPECT_NEAR(mesh->primitives[0].vertices[0].z, 1.0f * kGlbVerticalScale, 1.0e-4f);
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
    writeU64(bytes, kModelCacheChecksumOffset, cacheChecksum(bytes));
    OpenRCT2::File::WriteAllBytes(cacheFile, bytes.data(), bytes.size());

    std::string error;
    const std::optional<CpuMesh> mesh = loadModelForDraw("pine", sources, dir.cache, error);
    ASSERT_TRUE(mesh.has_value()) << error;
    EXPECT_NEAR(mesh->primitives[0].vertices[0].z, 1.0f * kGlbVerticalScale, 1.0e-4f);
    const std::vector<uint8_t> rebuilt = cacheBytes(dir, "pine");
    EXPECT_EQ(readU32(rebuilt, kModelCacheConverterOffset), kGlbConverterVersion);

    bytes = rebuilt;
    writeU32(bytes, kModelCacheMaxVerticesOffset, 1);
    writeU64(bytes, kModelCacheChecksumOffset, cacheChecksum(bytes));
    OpenRCT2::File::WriteAllBytes(cacheFile, bytes.data(), bytes.size());
    const std::optional<CpuMesh> again = loadModelForDraw("pine", sources, dir.cache, error);
    ASSERT_TRUE(again.has_value()) << error;
    EXPECT_EQ(readU32(cacheBytes(dir, "pine"), kModelCacheMaxVerticesOffset), 65535u);
}

TEST(ModelCache, RejectsUnsafeIdentifiers)
{
    EXPECT_TRUE(identifierIsSafe("rct2.scenery_small.tcrp"));
    EXPECT_TRUE(identifierIsSafe("rct2.scenery_small.trf2"));
    EXPECT_TRUE(identifierIsSafe("openrct2.ride.hybrid_coaster"));
    EXPECT_TRUE(identifierIsSafe("tygrysek90.station.abstract-open"));

    const char* names[] = { "CON",     "con",    "PRN",    "AUX",   "NUL",  "COM1", "com9", "LPT1", "lpt9",
                            "con.txt", "CONIN$", "bad:id", "a/b",   "a\\b", "a<b",  "a>b",  "a\"b", "a|b",
                            "a?b",     "a*b",    "trail.", ".lead", "a ",   "COM0", "",     "A",    "has space" };
    for (const char* name : names)
    {
        EXPECT_FALSE(identifierIsSafe(name)) << name;
    }
    // com0 is not a reserved device. Uppercase is not an object id, so it stays rejected above.
    EXPECT_TRUE(identifierIsSafe("com0"));
}

TEST(ModelCache, DirectoriesAreUnique)
{
    CacheDir first;
    CacheDir second;
    EXPECT_NE(first.root, second.root);
}

TEST(ModelCache, TamperedCountFallsBackToGlb)
{
    CacheDir dir;
    dir.writeGlb("pine.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    ASSERT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);

    std::vector<uint8_t> bytes = cacheBytes(dir, "pine");
    const std::vector<uint8_t> body(bytes.begin() + kModelCacheHeaderBytes, bytes.end());
    ASSERT_FALSE(body.empty());
    bytes.insert(bytes.end(), body.begin(), body.end());
    writeU32(bytes, kModelCachePrimitiveCountOffset, 2);
    writeU64(bytes, kModelCacheChecksumOffset, cacheChecksum(bytes));
    const std::string cacheFile = OpenRCT2::Path::Combine(dir.cache, "pine.o3d");
    OpenRCT2::File::WriteAllBytes(cacheFile, bytes.data(), bytes.size());

    std::string error;
    const std::optional<CpuMesh> doubled = loadModelForDraw("pine", sources, dir.cache, error);
    ASSERT_TRUE(doubled.has_value()) << error;
    EXPECT_EQ(doubled->primitives.size(), 2u);

    writeU32(bytes, kModelCachePrimitiveCountOffset, 1);
    OpenRCT2::File::WriteAllBytes(cacheFile, bytes.data(), bytes.size());
    const std::optional<CpuMesh> mesh = loadModelForDraw("pine", sources, dir.cache, error);
    ASSERT_TRUE(mesh.has_value()) << error;
    EXPECT_EQ(mesh->primitives.size(), 1u);
    const std::vector<uint8_t> rebuilt = cacheBytes(dir, "pine");
    EXPECT_EQ(readU32(rebuilt, kModelCachePrimitiveCountOffset), 1u);
    EXPECT_LT(rebuilt.size(), bytes.size());
}

TEST(ModelCache, TrailingBytesFallBackToGlb)
{
    CacheDir dir;
    dir.writeGlb("pine.glb", triangleGlb(1.0f));
    const std::vector<std::string> sources = { dir.source };
    ASSERT_EQ(prepareModelCache(sources, dir.cache, nullptr).converted, 1);

    const std::vector<uint8_t> original = cacheBytes(dir, "pine");
    std::vector<uint8_t> bytes = original;
    bytes.push_back(0xAB);
    writeU64(bytes, kModelCacheChecksumOffset, cacheChecksum(bytes));
    const std::string cacheFile = OpenRCT2::Path::Combine(dir.cache, "pine.o3d");
    OpenRCT2::File::WriteAllBytes(cacheFile, bytes.data(), bytes.size());

    std::string error;
    const std::optional<CpuMesh> mesh = loadModelForDraw("pine", sources, dir.cache, error);
    ASSERT_TRUE(mesh.has_value()) << error;
    EXPECT_EQ(mesh->primitives.size(), 1u);
    EXPECT_EQ(cacheBytes(dir, "pine"), original);
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
    ASSERT_EQ(fileModifiedNs(glbPath) / 1000000000uLL, bumped / 1000000000uLL);

    const ModelCacheStats stats = prepareModelCache(sources, dir.cache, nullptr);
    EXPECT_EQ(stats.converted, 0);
    EXPECT_EQ(stats.skipped, 1);
    EXPECT_EQ(stats.failed, 0);

    const std::vector<uint8_t> after = cacheBytes(dir, "pine");
    EXPECT_EQ(readU64(after, kModelCacheMtimeOffset), fileModifiedNs(glbPath));
    EXPECT_NE(readU64(after, kModelCacheMtimeOffset), stored);
    ASSERT_GT(before.size(), kModelCacheHeaderBytes);
    EXPECT_EQ(
        std::vector<uint8_t>(after.begin() + kModelCacheHeaderBytes, after.end()),
        std::vector<uint8_t>(before.begin() + kModelCacheHeaderBytes, before.end()));
}

TEST(ModelCache, SameSecondEditIsNotStale)
{
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
    const uint64_t updated = fileModifiedNs(glbPath);
    ASSERT_EQ(updated / 1000000000uLL, second);
    ASSERT_NE(updated, stored);

    std::string error;
    const std::optional<CpuMesh> mesh = loadModelForDraw("pine", sources, dir.cache, error);
    ASSERT_TRUE(mesh.has_value()) << error;
    EXPECT_NEAR(mesh->primitives[0].vertices[0].z, 2.0f * kGlbVerticalScale, 1.0e-4f);
}
