/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "GlbMesh.hpp"
#include "SpriteQuad.hpp"

#include <cstdint>
#include <vector>

namespace OpenRCT2::View3D
{
    struct MeshInstance
    {
        float x;
        float y;
        float z;
        float yaw;
    };

    // One distant sprite on a shared atlas page. The quad is a unit face; these
    // fields size it and pick the atlas rect. Twelve floats, no padding.
    struct DistantSpriteInstance
    {
        float x;
        float y;
        float z;
        float unused;
        float left;
        float right;
        float bottom;
        float top;
        float uvScaleX;
        float uvScaleY;
        float uvOffX;
        float uvOffY;
    };

    // Freed sprite ids stay retired until the next frame so a batch cannot draw a new image
    // with the id it already queued.
    class SpriteIdPool
    {
    public:
        void release(int32_t id)
        {
            _retired.push_back(id);
        }
        int32_t acquire()
        {
            if (_free.empty())
            {
                return -1;
            }
            const int32_t id = _free.back();
            _free.pop_back();
            return id;
        }
        void nextFrame()
        {
            _free.insert(_free.end(), _retired.begin(), _retired.end());
            _retired.clear();
        }
        void clear()
        {
            _free.clear();
            _retired.clear();
        }

    private:
        std::vector<int32_t> _free;
        std::vector<int32_t> _retired;
    };

    struct SpritePlace
    {
        int32_t id = -1;
        int32_t page = -1;
        float uvScaleX = 1.0f;
        float uvScaleY = 1.0f;
        float uvOffX = 0.0f;
        float uvOffY = 0.0f;
    };

    // Filament materials and GPU buffers for GLB meshes and crossed sprites.
    // Safe to call shutdown before device creation, and after a failed init.
    bool meshDrawInit();
    void meshDrawShutdown();

    enum class MeshUploadStatus : uint8_t
    {
        ready,
        // Atlas, slab, or handle cap. Evict and retry later.
        capacity,
        // The bytes are not a drawable mesh. Do not retry.
        rejected,
    };

    struct MeshUpload
    {
        MeshUploadStatus status = MeshUploadStatus::rejected;
        int32_t id = -1;
    };

    MeshUpload meshDrawUploadModel(const CpuMesh& mesh);
    uint32_t meshDrawGpuBytes(const CpuMesh& mesh);
    // Frees one uploaded model's ranges and any texture it owns. The id can be reused.
    void meshDrawReleaseModel(int32_t id);
    // Drops sprite textures. Called on park load so the previous park's images do not linger.
    void meshDrawClearSprites();

    // One face. The quad is in local space and instances only translate it (yaw 0).
    // A private texture is created only when `allowDedicated` is set and the atlas is full.
    SpritePlace meshDrawUploadSprite(
        const SpriteQuad& quad, const uint8_t* rgba, int32_t width, int32_t height, bool allowDedicated);
    void meshDrawReleaseSprite(int32_t id);
    // Retired sprite ids become reusable. Call once at the start of a frame.
    void meshDrawBeginFrame();
    // One instanced draw of every distant sprite on this atlas page and face.
    void meshDrawDistantSprites(int32_t page, int face, const DistantSpriteInstance* instances, uint32_t count);

    // Clamp folds an interpolated UV into 0..1 before the atlas rect. Repeat leaves
    // the interpolated value alone so a GL_REPEAT texture wraps it. fract() on the
    // vertices collapses a 0..2 face to a single texel.
    inline float wrapTexcoord(float uv, bool clampToEdge)
    {
        if (!clampToEdge)
        {
            return uv;
        }
        if (uv < 0.0f)
        {
            return 0.0f;
        }
        if (uv > 1.0f)
        {
            return 1.0f;
        }
        return uv;
    }

    inline float mapAtlasUv(float uv, bool clampToEdge, float scale, float offset)
    {
        return wrapTexcoord(uv, clampToEdge) * scale + offset;
    }

    void meshDrawModel(int32_t id, const MeshInstance* instances, uint32_t count);
    void meshDrawSprite(int32_t id, const MeshInstance* instances, uint32_t count);
} // namespace OpenRCT2::View3D
