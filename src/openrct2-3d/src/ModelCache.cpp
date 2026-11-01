/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "ModelCache.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <exception>
#include <openrct2/Diagnostic.h>
#include <openrct2/core/File.h>
#include <openrct2/core/FileScanner.h>
#include <openrct2/core/Path.hpp>
#include <unordered_set>

#ifdef _WIN32
    #include <process.h>
#else
    #include <unistd.h>
#endif

namespace OpenRCT2::View3D
{
    namespace
    {
        constexpr char kMagic[4] = { 'O', '3', 'D', 'M' };
        static_assert(kModelCacheHeaderBytes == 68, "header layout changed; bump kModelCacheVersion");
        static_assert(std::endian::native == std::endian::little, "the scenery cache is little-endian");

        // Header, little-endian:
        //  0 magic, 4 format version, 8 source size, 16 source mtime, 24 source hash,
        // 32 body checksum, 40 converter version, 44 maxVertices, 48 maxIndices,
        // 52 maxPrimitives, 56 maxImageBytes, 60 primitive count, 64 image count.
        // The body checksum is FNV-1a of every byte after the header.

        struct SourceStamp
        {
            uint64_t size = 0;
            uint64_t mtime = 0;
            uint64_t hash = 0;
        };

        enum class CacheAction
        {
            converted,
            skipped,
            failed,
        };

        bool limitsMatch(uint32_t maxVertices, uint32_t maxIndices, uint32_t maxPrimitives, uint32_t maxImageBytes);

        uint64_t hashBytes(const uint8_t* data, size_t size)
        {
            uint64_t hash = 14695981039346656037ull;
            for (size_t i = 0; i < size; i++)
            {
                hash ^= data[i];
                hash *= 1099511628211ull;
            }
            return hash;
        }

        std::string lowerCopy(std::string_view text)
        {
            std::string out(text);
            for (char& ch : out)
            {
                if (ch >= 'A' && ch <= 'Z')
                {
                    ch = static_cast<char>(ch - 'A' + 'a');
                }
            }
            return out;
        }

        bool isReservedWindowsName(std::string_view name)
        {
            std::string base = lowerCopy(name);
            while (!base.empty() && (base.back() == ' ' || base.back() == '.'))
            {
                base.pop_back();
            }
            const size_t dot = base.find('.');
            if (dot != std::string::npos)
            {
                base.resize(dot);
            }
            if (base == "con" || base == "prn" || base == "aux" || base == "nul")
            {
                return true;
            }
            if (base.size() == 4 && (base.compare(0, 3, "com") == 0 || base.compare(0, 3, "lpt") == 0) && base[3] >= '1'
                && base[3] <= '9')
            {
                return true;
            }
            return false;
        }

        bool identifierIsSafe(std::string_view identifier)
        {
            return !identifier.empty() && identifier.find('/') == std::string_view::npos
                && identifier.find('\\') == std::string_view::npos && identifier.find(':') == std::string_view::npos
                && identifier.find("..") == std::string_view::npos && !isReservedWindowsName(identifier);
        }

        std::string temporaryCachePath(const std::string& cachePath)
        {
            static std::atomic<uint32_t> sequence{ 0 };
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
            const int pid = _getpid();
#else
            const int pid = getpid();
#endif
            return cachePath + "." + std::to_string(pid) + "." + std::to_string(ticks) + "."
                + std::to_string(sequence.fetch_add(1)) + ".tmp";
        }

        void appendU32(std::vector<uint8_t>& out, uint32_t value)
        {
            out.push_back(static_cast<uint8_t>(value));
            out.push_back(static_cast<uint8_t>(value >> 8));
            out.push_back(static_cast<uint8_t>(value >> 16));
            out.push_back(static_cast<uint8_t>(value >> 24));
        }

        void appendU64(std::vector<uint8_t>& out, uint64_t value)
        {
            appendU32(out, static_cast<uint32_t>(value));
            appendU32(out, static_cast<uint32_t>(value >> 32));
        }

        void appendF32(std::vector<uint8_t>& out, float value)
        {
            uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            appendU32(out, bits);
        }

        class Reader
        {
            const uint8_t* _data;
            size_t _size;
            size_t _pos = 0;
            bool _ok = true;

        public:
            Reader(const uint8_t* data, size_t size)
                : _data(data)
                , _size(size)
            {
            }

            bool ok() const
            {
                return _ok;
            }

            uint32_t u32()
            {
                if (!need(4))
                {
                    return 0;
                }
                const uint32_t value = static_cast<uint32_t>(_data[_pos]) | (static_cast<uint32_t>(_data[_pos + 1]) << 8)
                    | (static_cast<uint32_t>(_data[_pos + 2]) << 16) | (static_cast<uint32_t>(_data[_pos + 3]) << 24);
                _pos += 4;
                return value;
            }

            uint64_t u64()
            {
                const uint64_t low = u32();
                const uint64_t high = u32();
                return low | (high << 32);
            }

            float f32()
            {
                const uint32_t bits = u32();
                float value = 0.0f;
                std::memcpy(&value, &bits, sizeof(value));
                return value;
            }

            void bytes(void* dest, size_t size)
            {
                if (!need(size))
                {
                    return;
                }
                std::memcpy(dest, _data + _pos, size);
                _pos += size;
            }

        private:
            bool need(size_t size)
            {
                if (!_ok || size > _size - _pos)
                {
                    _ok = false;
                    return false;
                }
                return true;
            }
        };

        std::string cachePathFor(const std::string& cacheDir, const std::string& identifier)
        {
            return Path::Combine(cacheDir, identifier + ".o3d");
        }

        SourceStamp stampOf(const std::string& path, const std::vector<uint8_t>& bytes)
        {
            SourceStamp stamp;
            stamp.size = bytes.size();
            stamp.mtime = File::GetLastModified(path);
            stamp.hash = hashBytes(bytes.data(), bytes.size());
            return stamp;
        }

        bool cacheIsCurrent(const std::string& cachePath, const SourceStamp& stamp)
        {
            if (!File::Exists(cachePath))
            {
                return false;
            }
            try
            {
                const std::vector<uint8_t> bytes = File::ReadAllBytes(cachePath);
                if (bytes.size() < kModelCacheHeaderBytes)
                {
                    return false;
                }
                Reader reader(bytes.data(), bytes.size());
                char magic[4] = {};
                reader.bytes(magic, 4);
                if (!reader.ok() || std::memcmp(magic, kMagic, 4) != 0)
                {
                    return false;
                }
                const uint32_t version = reader.u32();
                const uint64_t size = reader.u64();
                const uint64_t mtime = reader.u64();
                const uint64_t hash = reader.u64();
                const uint64_t bodyChecksum = reader.u64();
                const uint32_t converter = reader.u32();
                const uint32_t maxVertices = reader.u32();
                const uint32_t maxIndices = reader.u32();
                const uint32_t maxPrimitives = reader.u32();
                const uint32_t maxImageBytes = reader.u32();
                if (!reader.ok() || version != kModelCacheVersion || converter != kGlbConverterVersion
                    || !limitsMatch(maxVertices, maxIndices, maxPrimitives, maxImageBytes) || size != stamp.size
                    || mtime != stamp.mtime || hash != stamp.hash || bytes.size() < kModelCacheHeaderBytes)
                {
                    return false;
                }
                const uint64_t actual = hashBytes(bytes.data() + kModelCacheHeaderBytes, bytes.size() - kModelCacheHeaderBytes);
                return actual == bodyChecksum;
            }
            catch (const std::exception&)
            {
                return false;
            }
        }

        bool limitsMatch(uint32_t maxVertices, uint32_t maxIndices, uint32_t maxPrimitives, uint32_t maxImageBytes)
        {
            const MeshLoadLimits limits;
            return maxVertices == limits.maxVertices && maxIndices == limits.maxIndices && maxPrimitives == limits.maxPrimitives
                && maxImageBytes == limits.maxImageBytes;
        }

        bool writeCache(const std::string& cachePath, const SourceStamp& stamp, const CpuMesh& mesh, std::string& error)
        {
            std::vector<uint8_t> body;
            body.reserve(1024);
            for (const MeshPrimitive& primitive : mesh.primitives)
            {
                appendU32(body, static_cast<uint32_t>(primitive.vertices.size()));
                appendU32(body, static_cast<uint32_t>(primitive.indices.size()));
                body.push_back(static_cast<uint8_t>(primitive.alpha));
                body.push_back(primitive.doubleSided ? 1 : 0);
                body.push_back(0);
                body.push_back(0);
                appendF32(body, primitive.alphaCutoff);
                appendU32(body, static_cast<uint32_t>(primitive.image));
                for (const MeshVertex& vertex : primitive.vertices)
                {
                    appendF32(body, vertex.x);
                    appendF32(body, vertex.y);
                    appendF32(body, vertex.z);
                    appendF32(body, vertex.nx);
                    appendF32(body, vertex.ny);
                    appendF32(body, vertex.nz);
                    appendF32(body, vertex.u);
                    appendF32(body, vertex.v);
                }
                for (uint32_t index : primitive.indices)
                {
                    appendU32(body, index);
                }
            }

            for (const MeshImage& image : mesh.images)
            {
                body.push_back(image.nearest ? 1 : 0);
                body.push_back(image.clamp ? 1 : 0);
                body.push_back(0);
                body.push_back(0);
                appendU32(body, image.width);
                appendU32(body, image.height);
                appendU32(body, static_cast<uint32_t>(image.png.size()));
                body.insert(body.end(), image.png.begin(), image.png.end());
                appendU32(body, static_cast<uint32_t>(image.rgba.size()));
                body.insert(body.end(), image.rgba.begin(), image.rgba.end());
            }

            const MeshLoadLimits limits;
            std::vector<uint8_t> out;
            out.reserve(kModelCacheHeaderBytes + body.size());
            out.insert(out.end(), kMagic, kMagic + 4);
            appendU32(out, kModelCacheVersion);
            appendU64(out, stamp.size);
            appendU64(out, stamp.mtime);
            appendU64(out, stamp.hash);
            appendU64(out, hashBytes(body.data(), body.size()));
            appendU32(out, kGlbConverterVersion);
            appendU32(out, limits.maxVertices);
            appendU32(out, limits.maxIndices);
            appendU32(out, limits.maxPrimitives);
            appendU32(out, limits.maxImageBytes);
            appendU32(out, static_cast<uint32_t>(mesh.primitives.size()));
            appendU32(out, static_cast<uint32_t>(mesh.images.size()));
            out.insert(out.end(), body.begin(), body.end());

            const std::string temporary = temporaryCachePath(cachePath);
            try
            {
                File::WriteAllBytes(temporary, out.data(), out.size());
                File::Delete(cachePath);
                if (!File::Move(temporary, cachePath))
                {
                    error = "could not replace cache file";
                    File::Delete(temporary);
                    return false;
                }
            }
            catch (const std::exception& ex)
            {
                error = ex.what();
                File::Delete(temporary);
                return false;
            }
            return true;
        }

        bool readByte(Reader& reader, uint8_t& value)
        {
            reader.bytes(&value, 1);
            return reader.ok();
        }

        struct CachedMesh
        {
            SourceStamp stamp;
            CpuMesh mesh;
        };

        std::optional<CachedMesh> readCache(const std::string& cachePath, std::string& error)
        {
            std::vector<uint8_t> bytes;
            try
            {
                bytes = File::ReadAllBytes(cachePath);
            }
            catch (const std::exception& ex)
            {
                error = ex.what();
                return std::nullopt;
            }
            Reader reader(bytes.data(), bytes.size());
            char magic[4] = {};
            reader.bytes(magic, 4);
            if (!reader.ok() || std::memcmp(magic, kMagic, 4) != 0)
            {
                error = "cache header is not a scenery model";
                return std::nullopt;
            }
            if (reader.u32() != kModelCacheVersion)
            {
                error = "cache version is old";
                return std::nullopt;
            }
            SourceStamp stamp;
            stamp.size = reader.u64();
            stamp.mtime = reader.u64();
            stamp.hash = reader.u64();
            const uint64_t bodyChecksum = reader.u64();
            const uint32_t converter = reader.u32();
            const uint32_t maxVertices = reader.u32();
            const uint32_t maxIndices = reader.u32();
            const uint32_t maxPrimitives = reader.u32();
            const uint32_t maxImageBytes = reader.u32();
            const uint32_t primitiveCount = reader.u32();
            const uint32_t imageCount = reader.u32();
            if (bytes.size() < kModelCacheHeaderBytes
                || hashBytes(bytes.data() + kModelCacheHeaderBytes, bytes.size() - kModelCacheHeaderBytes) != bodyChecksum)
            {
                error = "cache checksum does not match";
                return std::nullopt;
            }
            if (converter != kGlbConverterVersion || !limitsMatch(maxVertices, maxIndices, maxPrimitives, maxImageBytes))
            {
                error = "cache was built by a different converter";
                return std::nullopt;
            }
            const MeshLoadLimits limits;
            if (!reader.ok() || primitiveCount == 0 || primitiveCount > limits.maxPrimitives || imageCount > 32)
            {
                error = "cache counts are unusable";
                return std::nullopt;
            }

            CpuMesh mesh;
            bool anyVertex = false;
            for (uint32_t primitiveIndex = 0; primitiveIndex < primitiveCount; primitiveIndex++)
            {
                const uint32_t vertexCount = reader.u32();
                const uint32_t indexCount = reader.u32();
                uint8_t alphaByte = 0;
                uint8_t doubleSided = 0;
                uint8_t pad0 = 0;
                uint8_t pad1 = 0;
                readByte(reader, alphaByte);
                readByte(reader, doubleSided);
                readByte(reader, pad0);
                readByte(reader, pad1);
                const float cutoff = reader.f32();
                const int32_t image = static_cast<int32_t>(reader.u32());
                if (!reader.ok() || alphaByte > static_cast<uint8_t>(MeshAlpha::blend) || vertexCount > limits.maxVertices
                    || indexCount > limits.maxIndices || image < -1 || image >= static_cast<int32_t>(imageCount))
                {
                    error = "cache primitive is unusable";
                    return std::nullopt;
                }
                MeshPrimitive primitive;
                primitive.alpha = static_cast<MeshAlpha>(alphaByte);
                primitive.doubleSided = doubleSided != 0;
                primitive.alphaCutoff = cutoff;
                primitive.image = image;
                primitive.vertices.resize(vertexCount);
                for (MeshVertex& vertex : primitive.vertices)
                {
                    vertex.x = reader.f32();
                    vertex.y = reader.f32();
                    vertex.z = reader.f32();
                    vertex.nx = reader.f32();
                    vertex.ny = reader.f32();
                    vertex.nz = reader.f32();
                    vertex.u = reader.f32();
                    vertex.v = reader.f32();
                    if (!anyVertex)
                    {
                        mesh.bounds.minX = mesh.bounds.maxX = vertex.x;
                        mesh.bounds.minY = mesh.bounds.maxY = vertex.y;
                        mesh.bounds.minZ = mesh.bounds.maxZ = vertex.z;
                        anyVertex = true;
                    }
                    else
                    {
                        mesh.bounds.minX = std::min(mesh.bounds.minX, vertex.x);
                        mesh.bounds.minY = std::min(mesh.bounds.minY, vertex.y);
                        mesh.bounds.minZ = std::min(mesh.bounds.minZ, vertex.z);
                        mesh.bounds.maxX = std::max(mesh.bounds.maxX, vertex.x);
                        mesh.bounds.maxY = std::max(mesh.bounds.maxY, vertex.y);
                        mesh.bounds.maxZ = std::max(mesh.bounds.maxZ, vertex.z);
                    }
                }
                primitive.indices.resize(indexCount);
                for (uint32_t& index : primitive.indices)
                {
                    index = reader.u32();
                    if (index >= vertexCount)
                    {
                        error = "cache index is out of range";
                        return std::nullopt;
                    }
                }
                if (!reader.ok())
                {
                    error = "cache ended early";
                    return std::nullopt;
                }
                mesh.primitives.push_back(std::move(primitive));
            }

            for (uint32_t imageIndex = 0; imageIndex < imageCount; imageIndex++)
            {
                uint8_t nearest = 0;
                uint8_t clamp = 0;
                uint8_t pad0 = 0;
                uint8_t pad1 = 0;
                readByte(reader, nearest);
                readByte(reader, clamp);
                readByte(reader, pad0);
                readByte(reader, pad1);
                const uint32_t width = reader.u32();
                const uint32_t height = reader.u32();
                const uint32_t pngSize = reader.u32();
                if (!reader.ok() || pngSize > limits.maxImageBytes)
                {
                    error = "cache image is unusable";
                    return std::nullopt;
                }
                MeshImage image;
                image.nearest = nearest != 0;
                image.clamp = clamp != 0;
                image.width = width;
                image.height = height;
                image.png.resize(pngSize);
                if (pngSize > 0)
                {
                    reader.bytes(image.png.data(), pngSize);
                }
                const uint32_t rgbaSize = reader.u32();
                if (!reader.ok() || rgbaSize > limits.maxImageBytes)
                {
                    error = "cache image is unusable";
                    return std::nullopt;
                }
                image.rgba.resize(rgbaSize);
                if (rgbaSize > 0)
                {
                    reader.bytes(image.rgba.data(), rgbaSize);
                }
                if (!reader.ok())
                {
                    error = "cache ended early";
                    return std::nullopt;
                }
                mesh.images.push_back(std::move(image));
            }
            return CachedMesh{ stamp, std::move(mesh) };
        }

        std::string findGlb(const std::string& identifier, const std::vector<std::string>& sourceDirs)
        {
            const std::string file = identifier + ".glb";
            for (const std::string& dir : sourceDirs)
            {
                const std::string path = Path::Combine(dir, file);
                if (File::Exists(path))
                {
                    return path;
                }
            }
            return {};
        }

        bool readFile(const std::string& path, std::vector<uint8_t>& bytes, std::string& error)
        {
            try
            {
                bytes = File::ReadAllBytes(path);
            }
            catch (const std::exception& ex)
            {
                error = ex.what();
                return false;
            }
            if (bytes.empty())
            {
                error = "empty file";
                return false;
            }
            return true;
        }

        CacheAction convertFile(const std::string& identifier, const std::string& glbPath, const std::string& cacheDir)
        {
            std::string error;
            std::vector<uint8_t> bytes;
            if (!readFile(glbPath, bytes, error))
            {
                LOG_ERROR("View3D: could not read %s (%s)", glbPath.c_str(), error.c_str());
                return CacheAction::failed;
            }
            const SourceStamp stamp = stampOf(glbPath, bytes);
            const std::string cachePath = cachePathFor(cacheDir, identifier);
            if (cacheIsCurrent(cachePath, stamp))
            {
                return CacheAction::skipped;
            }
            const std::optional<CpuMesh> mesh = loadGlbMesh(bytes.data(), bytes.size(), {}, error);
            if (!mesh.has_value())
            {
                LOG_ERROR("View3D: %s is not a usable model (%s)", identifier.c_str(), error.c_str());
                File::Delete(cachePath);
                return CacheAction::failed;
            }
            if (!writeCache(cachePath, stamp, *mesh, error))
            {
                LOG_ERROR("View3D: could not cache %s (%s)", identifier.c_str(), error.c_str());
                return CacheAction::failed;
            }
            return CacheAction::converted;
        }

        std::vector<std::pair<std::string, std::string>> collectGlbs(const std::vector<std::string>& sourceDirs)
        {
            std::vector<std::pair<std::string, std::string>> files;
            std::unordered_set<std::string> seen;
            for (const std::string& dir : sourceDirs)
            {
                if (dir.empty() || !Path::DirectoryExists(dir))
                {
                    continue;
                }
                const std::unique_ptr<IFileScanner> scanner = Path::scanDirectory(Path::Combine(dir, u8"*.glb"), false);
                if (scanner == nullptr)
                {
                    continue;
                }
                while (scanner->next())
                {
                    const std::string path = scanner->getPath();
                    const std::string identifier = Path::GetFileNameWithoutExtension(path);
                    if (!identifierIsSafe(identifier) || !seen.insert(identifier).second)
                    {
                        continue;
                    }
                    files.emplace_back(identifier, path);
                }
            }
            return files;
        }
    } // namespace

    ModelCacheStats prepareModelCache(
        const std::vector<std::string>& sourceDirs, const std::string& cacheDir, ModelCacheProgressFn progress)
    {
        ModelCacheStats stats;
        try
        {
            if (!cacheDir.empty())
            {
                Path::CreateDirectory(cacheDir);
            }
            const std::vector<std::pair<std::string, std::string>> files = collectGlbs(sourceDirs);
            const int32_t total = static_cast<int32_t>(files.size());
            for (int32_t index = 0; index < total; index++)
            {
                const std::string& identifier = files[static_cast<size_t>(index)].first;
                const std::string& path = files[static_cast<size_t>(index)].second;
                CacheAction action = CacheAction::failed;
                try
                {
                    action = cacheDir.empty() ? CacheAction::failed : convertFile(identifier, path, cacheDir);
                }
                catch (const std::exception& ex)
                {
                    LOG_ERROR("View3D: %s failed (%s)", identifier.c_str(), ex.what());
                }
                switch (action)
                {
                    case CacheAction::converted:
                        stats.converted++;
                        break;
                    case CacheAction::skipped:
                        stats.skipped++;
                        break;
                    case CacheAction::failed:
                        stats.failed++;
                        break;
                }
                if (progress != nullptr)
                {
                    progress(index + 1, total, identifier);
                }
            }
        }
        catch (const std::exception& ex)
        {
            LOG_ERROR("View3D: scenery model cache failed (%s)", ex.what());
        }
        return stats;
    }

    std::optional<CpuMesh> loadModelForDraw(
        const std::string& identifier, const std::vector<std::string>& sourceDirs, const std::string& cacheDir,
        std::string& error)
    {
        error.clear();
        if (!identifierIsSafe(identifier))
        {
            error = "object id is not a safe file name";
            return std::nullopt;
        }
        const std::string glbPath = findGlb(identifier, sourceDirs);
        if (glbPath.empty())
        {
            return std::nullopt;
        }
        const uint64_t glbSize = File::GetSize(glbPath);
        const uint64_t glbMtime = File::GetLastModified(glbPath);
        const std::string cachePath = cacheDir.empty() ? std::string() : cachePathFor(cacheDir, identifier);
        std::string cacheError;
        // One read of the .o3d. Size and mtime are enough when they still match; the
        // body checksum is checked inside readCache. The GLB is hashed only if those differ.
        const std::optional<CachedMesh> cached = cachePath.empty() ? std::nullopt : readCache(cachePath, cacheError);
        if (cached.has_value() && cached->stamp.size == glbSize && cached->stamp.mtime == glbMtime)
        {
            return cached->mesh;
        }
        std::vector<uint8_t> bytes;
        if (!readFile(glbPath, bytes, error))
        {
            return std::nullopt;
        }
        const SourceStamp stamp = stampOf(glbPath, bytes);
        if (cached.has_value() && cached->stamp.size == stamp.size && cached->stamp.hash == stamp.hash)
        {
            std::string writeError;
            Path::CreateDirectory(cacheDir);
            writeCache(cachePath, stamp, cached->mesh, writeError);
            return cached->mesh;
        }
        if (!cacheError.empty() && File::Exists(cachePath))
        {
            LOG_WARNING("View3D: cache for %s is unusable (%s)", identifier.c_str(), cacheError.c_str());
        }
        const std::optional<CpuMesh> mesh = loadGlbMesh(bytes.data(), bytes.size(), {}, error);
        if (!mesh.has_value())
        {
            if (!cachePath.empty())
            {
                File::Delete(cachePath);
            }
            return std::nullopt;
        }
        if (!cachePath.empty())
        {
            std::string writeError;
            Path::CreateDirectory(cacheDir);
            if (!writeCache(cachePath, stamp, *mesh, writeError))
            {
                LOG_WARNING("View3D: could not cache %s (%s)", identifier.c_str(), writeError.c_str());
            }
        }
        return mesh;
    }
} // namespace OpenRCT2::View3D
