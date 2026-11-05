/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include <openrct2-ui/drawing/View3D.h>

#include <cstddef>

namespace OpenRCT2::View3D
{
    inline bool compositeIndexedUi(const uint8_t* indexed, int32_t width, int32_t height, int32_t pitch,
        const ColourBGRA* palette, uint8_t* rgba)
    {
        if (indexed == nullptr || palette == nullptr || rgba == nullptr || width <= 0 || height <= 0 || pitch < width)
        {
            return false;
        }
        for (int32_t y = 0; y < height; ++y)
        {
            const uint8_t* row = indexed + static_cast<size_t>(y) * static_cast<size_t>(pitch);
            uint8_t* destination = rgba + static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
            for (int32_t x = 0; x < width; ++x, destination += 4)
            {
                const uint8_t index = row[x];
                if (index != 0)
                {
                    const auto& colour = palette[index];
                    destination[0] = colour.red;
                    destination[1] = colour.green;
                    destination[2] = colour.blue;
                    destination[3] = 255;
                }
            }
        }
        return true;
    }
}
