/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "GlbMesh.hpp"

#include "ViewerScale.hpp"

#include <algorithm>
#include <cgltf.h>
#include <cmath>
#include <fstream>
#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4505)
#endif
#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wunused-function"
#endif
#include <stb_image.h>
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif

namespace OpenRCT2::View3D
{
    namespace
    {
        constexpr int32_t kNearest = 9728;
        constexpr int32_t kClampToEdge = 33071;

        const char* resultName(cgltf_result result)
        {
            switch (result)
            {
                case cgltf_result_success:
                    return "success";
                case cgltf_result_data_too_short:
                    return "data too short";
                case cgltf_result_unknown_format:
                    return "unknown format";
                case cgltf_result_invalid_json:
                    return "invalid json";
                case cgltf_result_invalid_gltf:
                    return "invalid gltf";
                case cgltf_result_invalid_options:
                    return "invalid options";
                case cgltf_result_file_not_found:
                    return "file not found";
                case cgltf_result_io_error:
                    return "io error";
                case cgltf_result_out_of_memory:
                    return "out of memory";
                case cgltf_result_legacy_gltf:
                    return "legacy gltf";
                default:
                    return "error";
            }
        }

        void transformPoint(const float m[16], float x, float y, float z, float& ox, float& oy, float& oz)
        {
            ox = m[0] * x + m[4] * y + m[8] * z + m[12];
            oy = m[1] * x + m[5] * y + m[9] * z + m[13];
            oz = m[2] * x + m[6] * y + m[10] * z + m[14];
        }

        // Inverse-transpose of the upper 3x3, so a scaled node still lights correctly.
        void transformNormal(const float m[16], float x, float y, float z, float& ox, float& oy, float& oz)
        {
            const float a00 = m[0];
            const float a01 = m[4];
            const float a02 = m[8];
            const float a10 = m[1];
            const float a11 = m[5];
            const float a12 = m[9];
            const float a20 = m[2];
            const float a21 = m[6];
            const float a22 = m[10];
            const float det = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);
            if (std::fabs(det) < 1.0e-12f)
            {
                ox = x;
                oy = y;
                oz = z;
                return;
            }
            const float inv = 1.0f / det;
            const float i00 = (a11 * a22 - a12 * a21) * inv;
            const float i01 = (a02 * a21 - a01 * a22) * inv;
            const float i02 = (a01 * a12 - a02 * a11) * inv;
            const float i10 = (a12 * a20 - a10 * a22) * inv;
            const float i11 = (a00 * a22 - a02 * a20) * inv;
            const float i12 = (a02 * a10 - a00 * a12) * inv;
            const float i20 = (a10 * a21 - a11 * a20) * inv;
            const float i21 = (a01 * a20 - a00 * a21) * inv;
            const float i22 = (a00 * a11 - a01 * a10) * inv;
            ox = i00 * x + i10 * y + i20 * z;
            oy = i01 * x + i11 * y + i21 * z;
            oz = i02 * x + i12 * y + i22 * z;
        }

        void normalise(float& x, float& y, float& z)
        {
            const float length = std::sqrt(x * x + y * y + z * z);
            if (length > 1.0e-8f)
            {
                x /= length;
                y /= length;
                z /= length;
            }
            else
            {
                x = 0.0f;
                y = 0.0f;
                z = 1.0f;
            }
        }

        const cgltf_accessor* findAttribute(const cgltf_primitive& primitive, cgltf_attribute_type type)
        {
            for (cgltf_size i = 0; i < primitive.attributes_count; i++)
            {
                if (primitive.attributes[i].type == type)
                {
                    return primitive.attributes[i].data;
                }
            }
            return nullptr;
        }

        int32_t imageIndexFor(const cgltf_data* data, const cgltf_material* material, std::string& error)
        {
            if (material == nullptr || material->has_pbr_metallic_roughness == 0)
            {
                return -1;
            }
            const cgltf_texture_view& view = material->pbr_metallic_roughness.base_color_texture;
            if (view.texture == nullptr || view.texture->image == nullptr)
            {
                return -1;
            }
            const cgltf_image* image = view.texture->image;
            if (image < data->images || image >= data->images + data->images_count)
            {
                error = "texture image is not in the file";
                return -2;
            }
            return static_cast<int32_t>(image - data->images);
        }

        bool readImage(const cgltf_data* data, int32_t index, const MeshLoadLimits& limits, MeshImage& out, std::string& error)
        {
            if (index < 0)
            {
                return true;
            }
            const cgltf_image& image = data->images[index];
            const cgltf_buffer_view* view = image.buffer_view;
            if (view == nullptr || view->size == 0)
            {
                error = "texture is not embedded";
                return false;
            }
            if (view->size > limits.maxImageBytes)
            {
                error = "texture is too large";
                return false;
            }
            const uint8_t* bytes = cgltf_buffer_view_data(view);
            if (bytes == nullptr)
            {
                error = "texture bytes are missing";
                return false;
            }
            out.png.assign(bytes, bytes + view->size);

            const cgltf_sampler* sampler = nullptr;
            for (cgltf_size i = 0; i < data->textures_count; i++)
            {
                if (data->textures[i].image == &image)
                {
                    sampler = data->textures[i].sampler;
                    break;
                }
            }
            if (sampler != nullptr)
            {
                out.nearest = sampler->mag_filter == kNearest && sampler->min_filter == kNearest;
                out.clamp = sampler->wrap_s == kClampToEdge && sampler->wrap_t == kClampToEdge;
            }
            return true;
        }

        bool readPrimitive(
            const cgltf_data* data, const cgltf_primitive& primitive, const float world[16], const MeshLoadLimits& limits,
            MeshPrimitive& out, std::string& error)
        {
            if (primitive.type != cgltf_primitive_type_triangles)
            {
                error = "primitive is not triangles";
                return false;
            }
            const cgltf_accessor* positions = findAttribute(primitive, cgltf_attribute_type_position);
            const cgltf_accessor* normals = findAttribute(primitive, cgltf_attribute_type_normal);
            const cgltf_accessor* uvs = findAttribute(primitive, cgltf_attribute_type_texcoord);
            if (positions == nullptr || positions->count == 0)
            {
                error = "primitive has no positions";
                return false;
            }
            if (positions->count > limits.maxVertices)
            {
                error = "too many vertices";
                return false;
            }

            out.vertices.resize(positions->count);
            for (cgltf_size i = 0; i < positions->count; i++)
            {
                // cgltf writes element_size floats to one contiguous array.
                float position[3] = {};
                if (!cgltf_accessor_read_float(positions, i, position, 3))
                {
                    error = "position read failed";
                    return false;
                }
                float wx;
                float wy;
                float wz;
                transformPoint(world, position[0], position[1], position[2], wx, wy, wz);
                const Vec3 view = gltfToViewer(wx, wy, wz);

                float normalIn[3] = { 0.0f, 0.0f, 1.0f };
                if (normals != nullptr && i < normals->count)
                {
                    cgltf_accessor_read_float(normals, i, normalIn, 3);
                }
                float nwx;
                float nwy;
                float nwz;
                transformNormal(world, normalIn[0], normalIn[1], normalIn[2], nwx, nwy, nwz);
                float vnx = nwx;
                float vny = -nwz;
                float vnz = nwy;
                // Position Z was scaled. Divide the normal's Z so lighting matches.
                if (kGlbVerticalScale != 0.0f)
                {
                    vnz /= kGlbVerticalScale;
                }
                normalise(vnx, vny, vnz);

                float uv[2] = {};
                if (uvs != nullptr && i < uvs->count)
                {
                    cgltf_accessor_read_float(uvs, i, uv, 2);
                }

                MeshVertex& vertex = out.vertices[i];
                vertex.x = view.x;
                vertex.y = view.y;
                vertex.z = view.z;
                vertex.nx = vnx;
                vertex.ny = vny;
                vertex.nz = vnz;
                vertex.u = uv[0];
                vertex.v = uv[1];
            }

            if (primitive.indices != nullptr)
            {
                if (primitive.indices->count > limits.maxIndices)
                {
                    error = "too many indices";
                    return false;
                }
                out.indices.resize(primitive.indices->count);
                for (cgltf_size i = 0; i < primitive.indices->count; i++)
                {
                    cgltf_uint index = 0;
                    if (!cgltf_accessor_read_uint(primitive.indices, i, &index, 1))
                    {
                        error = "index read failed";
                        return false;
                    }
                    if (index >= positions->count)
                    {
                        error = "index out of range";
                        return false;
                    }
                    out.indices[i] = index;
                }
            }
            else
            {
                if (positions->count % 3 != 0)
                {
                    error = "triangle list is not a multiple of 3";
                    return false;
                }
                out.indices.resize(positions->count);
                for (cgltf_size i = 0; i < positions->count; i++)
                {
                    out.indices[i] = static_cast<uint32_t>(i);
                }
            }

            const cgltf_material* material = primitive.material;
            out.alpha = MeshAlpha::opaque;
            out.alphaCutoff = 0.0f;
            out.doubleSided = false;
            if (material != nullptr)
            {
                out.doubleSided = material->double_sided != 0;
                if (material->alpha_mode == cgltf_alpha_mode_mask)
                {
                    out.alpha = MeshAlpha::mask;
                    out.alphaCutoff = material->alpha_cutoff;
                }
                else if (material->alpha_mode == cgltf_alpha_mode_blend)
                {
                    out.alpha = MeshAlpha::blend;
                    out.alphaCutoff = 0.0f;
                }
            }
            const int32_t image = imageIndexFor(data, material, error);
            if (image == -2)
            {
                return false;
            }
            out.image = image;
            return true;
        }

        void expandBounds(MeshBounds& bounds, const MeshVertex& vertex, bool& any)
        {
            if (!any)
            {
                bounds.minX = bounds.maxX = vertex.x;
                bounds.minY = bounds.maxY = vertex.y;
                bounds.minZ = bounds.maxZ = vertex.z;
                any = true;
                return;
            }
            bounds.minX = std::min(bounds.minX, vertex.x);
            bounds.minY = std::min(bounds.minY, vertex.y);
            bounds.minZ = std::min(bounds.minZ, vertex.z);
            bounds.maxX = std::max(bounds.maxX, vertex.x);
            bounds.maxY = std::max(bounds.maxY, vertex.y);
            bounds.maxZ = std::max(bounds.maxZ, vertex.z);
        }
    } // namespace

    std::optional<CpuMesh> loadGlbMesh(const uint8_t* data, size_t size, const MeshLoadLimits& limits, std::string& error)
    {
        if (data == nullptr || size == 0)
        {
            error = "empty file";
            return std::nullopt;
        }

        cgltf_options options = {};
        cgltf_data* gltf = nullptr;
        const cgltf_result parsed = cgltf_parse(&options, data, size, &gltf);
        if (parsed != cgltf_result_success || gltf == nullptr)
        {
            error = resultName(parsed);
            return std::nullopt;
        }
        const cgltf_result loaded = cgltf_load_buffers(&options, gltf, nullptr);
        if (loaded != cgltf_result_success)
        {
            error = resultName(loaded);
            cgltf_free(gltf);
            return std::nullopt;
        }
        const cgltf_result valid = cgltf_validate(gltf);
        if (valid != cgltf_result_success)
        {
            error = resultName(valid);
            cgltf_free(gltf);
            return std::nullopt;
        }

        CpuMesh mesh;
        mesh.images.resize(gltf->images_count);
        for (cgltf_size i = 0; i < gltf->images_count; i++)
        {
            if (!readImage(gltf, static_cast<int32_t>(i), limits, mesh.images[i], error))
            {
                cgltf_free(gltf);
                return std::nullopt;
            }
        }

        bool anyVertex = false;
        for (cgltf_size nodeIndex = 0; nodeIndex < gltf->nodes_count; nodeIndex++)
        {
            const cgltf_node& node = gltf->nodes[nodeIndex];
            if (node.mesh == nullptr)
            {
                continue;
            }
            float world[16];
            cgltf_node_transform_world(&node, world);
            for (cgltf_size primitiveIndex = 0; primitiveIndex < node.mesh->primitives_count; primitiveIndex++)
            {
                if (mesh.primitives.size() >= limits.maxPrimitives)
                {
                    error = "too many primitives";
                    cgltf_free(gltf);
                    return std::nullopt;
                }
                MeshPrimitive primitive;
                if (!readPrimitive(gltf, node.mesh->primitives[primitiveIndex], world, limits, primitive, error))
                {
                    cgltf_free(gltf);
                    return std::nullopt;
                }
                for (const MeshVertex& vertex : primitive.vertices)
                {
                    expandBounds(mesh.bounds, vertex, anyVertex);
                }
                mesh.primitives.push_back(std::move(primitive));
            }
        }

        cgltf_free(gltf);
        if (mesh.primitives.empty())
        {
            error = "no mesh";
            return std::nullopt;
        }
        return mesh;
    }

    std::optional<CpuMesh> loadGlbFile(const std::string& path, const MeshLoadLimits& limits, std::string& error)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            error = "could not open file";
            return std::nullopt;
        }
        in.seekg(0, std::ios::end);
        const std::streamoff length = in.tellg();
        if (length <= 0)
        {
            error = "empty file";
            return std::nullopt;
        }
        in.seekg(0, std::ios::beg);
        std::vector<uint8_t> bytes(static_cast<size_t>(length));
        in.read(reinterpret_cast<char*>(bytes.data()), length);
        if (!in)
        {
            error = "could not read file";
            return std::nullopt;
        }
        return loadGlbMesh(bytes.data(), bytes.size(), limits, error);
    }

    bool decodeMeshImages(CpuMesh& mesh, std::string& error)
    {
        const MeshLoadLimits limits;
        for (MeshImage& image : mesh.images)
        {
            if (image.png.empty())
            {
                continue;
            }
            int width = 0;
            int height = 0;
            int components = 0;
            if (stbi_info_from_memory(image.png.data(), static_cast<int>(image.png.size()), &width, &height, &components) == 0)
            {
                error = "png header is unreadable";
                return false;
            }
            if (width <= 0 || height <= 0 || static_cast<uint32_t>(width) > limits.maxImageEdge
                || static_cast<uint32_t>(height) > limits.maxImageEdge)
            {
                error = "png dimensions are over the limit";
                return false;
            }
            const uint64_t bytes = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 4u;
            if (bytes > limits.maxImageBytes)
            {
                error = "png bitmap is over the limit";
                return false;
            }
            stbi_uc* pixels = stbi_load_from_memory(
                image.png.data(), static_cast<int>(image.png.size()), &width, &height, &components, 4);
            if (pixels == nullptr)
            {
                error = "png could not be decoded";
                return false;
            }
            image.width = static_cast<uint32_t>(width);
            image.height = static_cast<uint32_t>(height);
            image.rgba.assign(pixels, pixels + bytes);
            stbi_image_free(pixels);
            image.png.clear();
            image.png.shrink_to_fit();
        }
        return true;
    }
} // namespace OpenRCT2::View3D
