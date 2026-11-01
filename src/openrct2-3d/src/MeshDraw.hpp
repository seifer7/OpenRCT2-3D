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

namespace OpenRCT2::View3D
{
    struct MeshInstance
    {
        float x;
        float y;
        float z;
        float yaw;
    };

    // Shaders and GPU buffers for GLB meshes and crossed sprites. Safe to call
    // shutdown before bgfx::init, and after a failed init.
    bool meshDrawInit();
    void meshDrawShutdown();

    // A non-negative id. -1 if the mesh or its textures could not be uploaded.
    int32_t meshDrawUploadModel(const CpuMesh& mesh);
    // Frees one uploaded model's ranges and any texture it owns. The id is left empty.
    void meshDrawReleaseModel(int32_t id);
    // Drops sprite textures. Called on park load so the previous park's images do not linger.
    void meshDrawClearSprites();

    // One face. The quad is in local space and instances only translate it (yaw 0).
    int32_t meshDrawUploadSprite(const SpriteQuad& quad, const uint8_t* rgba, int32_t width, int32_t height);

    void meshDrawModel(int32_t id, const MeshInstance* instances, uint32_t count);
    void meshDrawSprite(int32_t id, const MeshInstance* instances, uint32_t count);
} // namespace OpenRCT2::View3D
