/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

namespace OpenRCT2::View3D::Park
{
    // Drops scenery meshes and sprite textures. Safe before bgfx::shutdown.
    void releaseScenery();

    // Small scenery. An object with a GLB is instanced from that mesh. Anything
    // else is four original direction sprites on two crossed quads. Submitted to
    // view 0, so the caller sets the camera first.
    void drawScenery();
} // namespace OpenRCT2::View3D::Park
