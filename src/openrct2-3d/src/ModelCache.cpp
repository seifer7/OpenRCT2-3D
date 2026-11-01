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
#include <cassert>
#include <chrono>
#include <cstring>
#include <exception>
#include <mutex>
#include <openrct2/Diagnostic.h>
#include <openrct2/core/File.h>
#include <openrct2/core/FileScanner.h>
#include <openrct2/core/FileStream.h>
#include <openrct2/core/Path.hpp>
#include <thread>
#include <unordered_map>
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
        constexpr const char* kOldCacheVersion = "cache version is old";
        static_assert(kModelCacheHeaderBytes == 68, "header layout changed; bump kModelCacheVersion");
        static_assert(std::endian::native == std::endian::little, "the scenery cache is little-endian");

        // Header, little-endian:
        //  0 magic, 4 format version, 8 source size, 16 source mtime, 24 source hash,
        // 32 checksum, 40 converter version, 44 maxVertices, 48 maxIndices,
        // 52 maxPrimitives, 56 maxImageBytes, 60 primitive count, 64 image count.
        // The checksum is FNV-1a of every byte except the checksum field itself
        // (bytes 0-31 and 40 through EOF). Modification time is nanoseconds.

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
            upgraded,
        };

        uint64_t hashBytes(const uint8_t* data, size_t size)
        {
            uint64_t hash = 14695981039346656037uLL;
            for (size_t i = 0; i < size; i++)
            {
                hash ^= data[i];
                hash *= 1099511628211uLL;
            }
            return hash;
        }

        // Covers the header counts and the body. The 8 checksum bytes at offset 32 are skipped.
        uint64_t checksumFile(const uint8_t* data, size_t size)
        {
            uint64_t hash = 14695981039346656037uLL;
            const auto feed = [&](size_t begin, size_t end) {
                for (size_t i = begin; i < end; i++)
                {
                    hash ^= data[i];
                    hash *= 1099511628211uLL;
                }
            };
            feed(0, std::min(size, kModelCacheChecksumOffset));
            if (size > kModelCacheChecksumOffset + 8)
            {
                feed(kModelCacheChecksumOffset + 8, size);
            }
            return hash;
        }

        void storeU64(std::vector<uint8_t>& out, size_t offset, uint64_t value)
        {
            for (size_t i = 0; i < 8; i++)
            {
                out[offset + i] = static_cast<uint8_t>(value >> (8 * i));
            }
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

            size_t position() const
            {
                return _pos;
            }

            size_t remaining() const
            {
                return _ok && _pos <= _size ? _size - _pos : 0;
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
            return Path::Combine(cacheDir, lowerCopy(identifier) + ".o3d");
        }

        SourceStamp stampOf(const std::string& path, const std::vector<uint8_t>& bytes)
        {
            SourceStamp stamp;
            stamp.size = bytes.size();
            stamp.mtime = File::GetLastModifiedNs(path);
            stamp.hash = hashBytes(bytes.data(), bytes.size());
            return stamp;
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
            appendU64(out, 0);
            appendU32(out, kGlbConverterVersion);
            appendU32(out, limits.maxVertices);
            appendU32(out, limits.maxIndices);
            appendU32(out, limits.maxPrimitives);
            appendU32(out, limits.maxImageBytes);
            appendU32(out, static_cast<uint32_t>(mesh.primitives.size()));
            appendU32(out, static_cast<uint32_t>(mesh.images.size()));
            out.insert(out.end(), body.begin(), body.end());
            storeU64(out, kModelCacheChecksumOffset, checksumFile(out.data(), out.size()));

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

        std::optional<CachedMesh> readCache(const std::string& cachePath, std::string& error, bool checkChecksum)
        {
            // One open. Exists + GetSize were a second and third stat on every in-game load.
            constexpr uint64_t kMaxCacheReadBytes = 64uLL << 20;
            std::vector<uint8_t> bytes;
            try
            {
                FileStream stream(cachePath, FileMode::open);
                const uint64_t fileSize = stream.GetLength();
                if (fileSize < kModelCacheHeaderBytes || fileSize > kMaxCacheReadBytes)
                {
                    error = "cache is missing or an unusual size";
                    return std::nullopt;
                }
                bytes.resize(static_cast<size_t>(fileSize));
                stream.Read(bytes.data(), fileSize);
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
            const uint32_t version = reader.u32();
            if (checkChecksum && version != kModelCacheVersion)
            {
                error = (version >= 1 && version < kModelCacheVersion) ? kOldCacheVersion
                                                                       : "cache version does not match this build";
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
            if (checkChecksum
                && (bytes.size() < kModelCacheHeaderBytes || checksumFile(bytes.data(), bytes.size()) != bodyChecksum))
            {
                error = "cache checksum does not match";
                return std::nullopt;
            }
            if (checkChecksum
                && (converter != kGlbConverterVersion || !limitsMatch(maxVertices, maxIndices, maxPrimitives, maxImageBytes)))
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
                const uint64_t geometryBytes = static_cast<uint64_t>(vertexCount) * 32uLL
                    + static_cast<uint64_t>(indexCount) * 4uLL;
                if (!reader.ok() || alphaByte > static_cast<uint8_t>(MeshAlpha::blend) || vertexCount > limits.maxVertices
                    || indexCount > limits.maxIndices || image < -1 || image >= static_cast<int32_t>(imageCount)
                    || geometryBytes > reader.remaining())
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
                const uint32_t rgbaSize = reader.u32();
                const uint64_t expected = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4u;
                if (!reader.ok() || width > limits.maxImageEdge || height > limits.maxImageEdge || rgbaSize != expected
                    || expected > limits.maxImageBytes || expected > reader.remaining())
                {
                    error = "cache image is unusable";
                    return std::nullopt;
                }
                MeshImage image;
                image.nearest = nearest != 0;
                image.clamp = clamp != 0;
                image.width = width;
                image.height = height;
                image.rgba.resize(static_cast<size_t>(rgbaSize));
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
            if (!reader.ok() || (checkChecksum && reader.position() != bytes.size()))
            {
                error = reader.ok() ? "cache has trailing bytes" : "cache ended early";
                return std::nullopt;
            }
            return CachedMesh{ stamp, std::move(mesh) };
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

        struct HeaderCheck
        {
            SourceStamp stamp;
            std::string error;
            bool present = false;
        };

        // Header only. A matching size and mtime does not build a CpuMesh.
        HeaderCheck readCacheHeader(const std::string& cachePath)
        {
            HeaderCheck header;
            if (!File::Exists(cachePath))
            {
                header.error = "cache is missing";
                return header;
            }
            try
            {
                FileStream stream(cachePath, FileMode::open);
                header.present = true;
                const uint64_t length = stream.GetLength();
                if (length < 8)
                {
                    header.error = "cache header is short";
                    return header;
                }
                uint8_t bytes[kModelCacheHeaderBytes] = {};
                const uint64_t toRead = std::min<uint64_t>(length, kModelCacheHeaderBytes);
                stream.Read(bytes, toRead);
                Reader reader(bytes, static_cast<size_t>(toRead));
                char magic[4] = {};
                reader.bytes(magic, 4);
                if (!reader.ok() || std::memcmp(magic, kMagic, 4) != 0)
                {
                    header.error = "cache header is not a scenery model";
                    return header;
                }
                const uint32_t version = reader.u32();
                if (version != kModelCacheVersion)
                {
                    // Versions 1..3 are the formats this branch wrote. Zero and any
                    // other value are a bit flip or a newer build, not "an older format".
                    const bool knownOlder = version >= 1 && version < kModelCacheVersion;
                    header.error = knownOlder ? kOldCacheVersion : "cache version does not match this build";
                    return header;
                }
                if (length < kModelCacheHeaderBytes)
                {
                    header.error = "cache header is short";
                    return header;
                }
                header.stamp.size = reader.u64();
                header.stamp.mtime = reader.u64();
                header.stamp.hash = reader.u64();
                reader.u64();
                const uint32_t converter = reader.u32();
                const uint32_t maxVertices = reader.u32();
                const uint32_t maxIndices = reader.u32();
                const uint32_t maxPrimitives = reader.u32();
                const uint32_t maxImageBytes = reader.u32();
                if (!reader.ok() || converter != kGlbConverterVersion
                    || !limitsMatch(maxVertices, maxIndices, maxPrimitives, maxImageBytes))
                {
                    header.error = "cache was built by a different converter";
                }
            }
            catch (const std::exception& ex)
            {
                header.error = ex.what();
            }
            return header;
        }

        bool refreshCacheTime(const std::string& cachePath, const SourceStamp& stamp, std::string& error)
        {
            std::vector<uint8_t> bytes;
            try
            {
                bytes = File::ReadAllBytes(cachePath);
            }
            catch (const std::exception& ex)
            {
                error = ex.what();
                return false;
            }
            if (bytes.size() < kModelCacheHeaderBytes)
            {
                error = "cache header is short";
                return false;
            }
            uint64_t stored = 0;
            for (size_t i = 0; i < 8; i++)
            {
                stored |= static_cast<uint64_t>(bytes[kModelCacheChecksumOffset + i]) << (8 * i);
            }
            if (checksumFile(bytes.data(), bytes.size()) != stored)
            {
                error = "cache checksum does not match";
                return false;
            }
            storeU64(bytes, kModelCacheSizeOffset, stamp.size);
            storeU64(bytes, kModelCacheMtimeOffset, stamp.mtime);
            storeU64(bytes, kModelCacheHashOffset, stamp.hash);
            storeU64(bytes, kModelCacheChecksumOffset, checksumFile(bytes.data(), bytes.size()));
            const std::string temporary = temporaryCachePath(cachePath);
            try
            {
                File::WriteAllBytes(temporary, bytes.data(), bytes.size());
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

        // Ids startup has accepted, keyed by authored case, valued by the cache path.
        // prepareModelCache is the only writer. In-game loads read that path and do not hash it again.
        std::unordered_map<std::string, std::string> _validated;
        std::mutex _validatedMutex;
        std::atomic<uint64_t> _checksumPasses{ 0 };

        void rememberValidated(const std::string& identifier, const std::string& cachePath);
        bool streamingChecksumOk(const std::string& path);

        CacheAction convertFile(const std::string& identifier, const std::string& glbPath, const std::string& cacheDir)
        {
            const uint64_t glbSize = File::GetSize(glbPath);
            const uint64_t glbMtime = File::GetLastModifiedNs(glbPath);
            const std::string cachePath = cachePathFor(cacheDir, identifier);
            const HeaderCheck header = readCacheHeader(cachePath);
            bool trustHeader = header.present && header.error.empty();
            if (trustHeader && header.stamp.size == glbSize && header.stamp.mtime == glbMtime)
            {
                // Size and mtime still match. Trust the cache. Do not hash the GLB or the body.
                rememberValidated(identifier, cachePath);
                return CacheAction::skipped;
            }

            std::string error;
            std::vector<uint8_t> bytes;
            if (!readFile(glbPath, bytes, error))
            {
                LOG_ERROR("View3D: could not read %s (%s)", glbPath.c_str(), error.c_str());
                return CacheAction::failed;
            }
            const SourceStamp stamp = stampOf(glbPath, bytes);
            if (trustHeader && header.stamp.size == stamp.size && header.stamp.hash == stamp.hash)
            {
                // Content hash matches. Rewrite the stored size and mtime. Do not reconvert.
                std::string writeError;
                if (refreshCacheTime(cachePath, stamp, writeError))
                {
                    rememberValidated(identifier, cachePath);
                    return CacheAction::skipped;
                }
                LOG_WARNING("View3D: could not refresh cache time for %s (%s)", identifier.c_str(), writeError.c_str());
            }
            const bool old = header.present && header.error == kOldCacheVersion;
            if (header.present && !header.error.empty() && !old)
            {
                LOG_WARNING("View3D: cache for %s is unusable (%s)", identifier.c_str(), header.error.c_str());
            }
            std::optional<CpuMesh> mesh = loadGlbMesh(bytes.data(), bytes.size(), {}, error);
            if (!mesh.has_value())
            {
                LOG_ERROR("View3D: %s is not a usable model (%s)", identifier.c_str(), error.c_str());
                File::Delete(cachePath);
                return CacheAction::failed;
            }
            if (!decodeMeshImages(*mesh, error))
            {
                LOG_ERROR("View3D: %s texture was rejected (%s)", identifier.c_str(), error.c_str());
                File::Delete(cachePath);
                return CacheAction::failed;
            }
            if (!writeCache(cachePath, stamp, *mesh, error))
            {
                LOG_ERROR("View3D: could not cache %s (%s)", identifier.c_str(), error.c_str());
                return CacheAction::failed;
            }
            rememberValidated(identifier, cachePath);
            return old ? CacheAction::upgraded : CacheAction::converted;
        }

        void rememberValidated(const std::string& identifier, const std::string& cachePath)
        {
            // Admission to the in-game set. loadModelForDraw must not call this.
            // Folded key: the first GLB wins, and either spelling finds that one cache.
            std::lock_guard<std::mutex> guard(_validatedMutex);
            _validated[lowerCopy(identifier)] = cachePath;
        }

        bool streamingChecksumOk(const std::string& path)
        {
            if (!File::Exists(path))
            {
                return false;
            }
            try
            {
                FileStream stream(path, FileMode::open);
                const uint64_t length = stream.GetLength();
                if (length < kModelCacheHeaderBytes)
                {
                    return false;
                }
                uint8_t header[kModelCacheHeaderBytes] = {};
                stream.Read(header, kModelCacheHeaderBytes);
                uint64_t stored = 0;
                for (size_t i = 0; i < 8; i++)
                {
                    stored |= static_cast<uint64_t>(header[kModelCacheChecksumOffset + i]) << (8 * i);
                }
                uint64_t hash = 14695981039346656037uLL;
                const auto feed = [&](const uint8_t* data, size_t count, uint64_t origin) {
                    for (size_t i = 0; i < count; i++)
                    {
                        const uint64_t at = origin + static_cast<uint64_t>(i);
                        if (at >= kModelCacheChecksumOffset && at < kModelCacheChecksumOffset + 8)
                        {
                            continue;
                        }
                        hash ^= data[i];
                        hash *= 1099511628211uLL;
                    }
                };
                feed(header, kModelCacheHeaderBytes, 0);
                uint8_t chunk[64u * 1024u];
                uint64_t origin = kModelCacheHeaderBytes;
                while (origin < length)
                {
                    const uint64_t want = std::min<uint64_t>(sizeof(chunk), length - origin);
                    stream.Read(chunk, want);
                    feed(chunk, static_cast<size_t>(want), origin);
                    origin += want;
                }
                _checksumPasses.fetch_add(1);
                return hash == stored;
            }
            catch (const std::exception&)
            {
                return false;
            }
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
                    if (!identifierIsSafe(identifier))
                    {
                        LOG_WARNING("View3D: ignoring %s (object id is not a usable file name)", identifier.c_str());
                        continue;
                    }
                    const std::string folded = lowerCopy(identifier);
                    if (!seen.insert(folded).second)
                    {
                        LOG_VERBOSE("View3D: ignoring %s (same id as an earlier file)", identifier.c_str());
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
        // Drop the previous session. Only this function rebuilds the validated set.
        {
            std::lock_guard<std::mutex> guard(_validatedMutex);
            _validated.clear();
        }
        try
        {
            if (!cacheDir.empty())
            {
                Path::CreateDirectory(cacheDir);
            }
            const std::vector<std::pair<std::string, std::string>> files = collectGlbs(sourceDirs);
            const int32_t total = static_cast<int32_t>(files.size());
            int32_t upgraded = 0;
            // Checksums and conversions of distinct files run together. Each file still
            // takes the size/mtime fast path and, on that path, still checksums the body.
            std::atomic<int32_t> nextIndex{ 0 };
            std::mutex statsMutex;
            const uint32_t workers = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
            std::vector<std::thread> pool;
            pool.reserve(workers);
            for (uint32_t worker = 0; worker < workers; worker++)
            {
                pool.emplace_back([&]() {
                    for (;;)
                    {
                        const int32_t index = nextIndex.fetch_add(1);
                        if (index >= total)
                        {
                            return;
                        }
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
                        std::lock_guard<std::mutex> guard(statsMutex);
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
                            case CacheAction::upgraded:
                                stats.converted++;
                                upgraded++;
                                break;
                        }
                        if (progress != nullptr)
                        {
                            progress(stats.converted + stats.skipped + stats.failed, total, identifier);
                        }
                    }
                });
            }
            for (std::thread& thread : pool)
            {
                thread.join();
            }
            if (upgraded > 0)
            {
                LOG_INFO("View3D: rebuilt %d scenery cache file(s) from an older format.", upgraded);
            }
        }
        catch (const std::exception& ex)
        {
            LOG_ERROR("View3D: scenery model cache failed (%s)", ex.what());
        }
        return stats;
    }

    uint64_t cacheFileChecksum(const uint8_t* data, size_t size)
    {
        return checksumFile(data, size);
    }

    uint64_t cacheSourceModifiedNs(const std::string& path)
    {
        return File::GetLastModifiedNs(path);
    }

    bool identifierIsSafe(std::string_view identifier)
    {
        if (identifier.empty() || identifier.front() == '.' || identifier.back() == '.')
        {
            return false;
        }
        for (unsigned char ch : identifier)
        {
            // JSON ids keep their authored case. Official ids are lowercase; custom ids may use A-Z.
            const bool allowed = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.'
                || ch == '_' || ch == '-';
            if (!allowed)
            {
                return false;
            }
        }
        return !isReservedWindowsName(identifier);
    }

    bool modelIsValidated(std::string_view identifier)
    {
        std::lock_guard<std::mutex> guard(_validatedMutex);
        return _validated.find(lowerCopy(identifier)) != _validated.end();
    }

    bool cacheChecksumMatchesFile(const std::string& path)
    {
        return streamingChecksumOk(path);
    }

    uint64_t cacheChecksumPassCount()
    {
        return _checksumPasses.load();
    }

    void resetCacheChecksumPassCount()
    {
        _checksumPasses.store(0);
    }

    std::optional<CpuMesh> loadModelForDraw(
        const std::string& identifier, const std::vector<std::string>&, const std::string&, std::string& error)
    {
        error.clear();
        if (!identifierIsSafe(identifier))
        {
            error = "object id is not a safe file name";
            return std::nullopt;
        }
        std::string cachePath;
        {
            std::lock_guard<std::mutex> guard(_validatedMutex);
            const auto found = _validated.find(lowerCopy(identifier));
            if (found == _validated.end())
            {
                return std::nullopt;
            }
            cachePath = found->second;
        }
        // Bounds checks only. Startup already checksummed this file. This assert
        // fails if a later edit starts hashing again on the draw path.
        const uint64_t checksumPasses = _checksumPasses.load();
        const std::optional<CachedMesh> cached = readCache(cachePath, error, false);
        assert(checksumPasses == _checksumPasses.load());
        (void)checksumPasses;
        if (!cached.has_value())
        {
            return std::nullopt;
        }
        return cached->mesh;
    }
} // namespace OpenRCT2::View3D
