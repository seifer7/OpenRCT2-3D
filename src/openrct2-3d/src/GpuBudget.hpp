/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#pragma once

#include "SceneryScale.hpp"

#include <cstdint>
#include <vector>

namespace OpenRCT2::View3D
{
    struct BufferRange
    {
        uint32_t slab = 0;
        uint32_t offset = 0;
        uint32_t count = 0;
    };

    // Sub-allocates vertex and index ranges inside a few shared slabs.
    // geometryHandles() is two per live slab (vertex buffer + index buffer).
    class GeometryArena
    {
    public:
        GeometryArena(uint32_t slabVertices, uint32_t slabIndices, uint32_t maxSlabs);
        bool alloc(uint32_t vertices, uint32_t indices, BufferRange& vertexRange, BufferRange& indexRange);
        void free(const BufferRange& vertexRange, const BufferRange& indexRange);
        uint32_t slabs() const
        {
            return static_cast<uint32_t>(_slabs.size());
        }
        uint32_t geometryHandles() const
        {
            return slabs() * 2u;
        }

    private:
        struct Span
        {
            uint32_t offset = 0;
            uint32_t count = 0;
        };
        struct Slab
        {
            std::vector<Span> vertices;
            std::vector<Span> indices;
        };
        bool take(std::vector<Span>& spans, uint32_t count, uint32_t& offset);
        void give(std::vector<Span>& spans, uint32_t offset, uint32_t count);
        uint32_t _slabVertices = 0;
        uint32_t _slabIndices = 0;
        uint32_t _maxSlabs = 0;
        std::vector<Slab> _slabs;
    };

    struct AtlasSlot
    {
        uint32_t page = 0;
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    // Shelf packer. pageCount() is the number of texture handles.
    class AtlasPacker
    {
    public:
        AtlasPacker(uint32_t pageSize, uint32_t maxPages);
        bool pack(uint32_t width, uint32_t height, AtlasSlot& slot);
        uint32_t pageCount() const
        {
            return static_cast<uint32_t>(_pages.size());
        }

    private:
        struct Shelf
        {
            uint32_t y = 0;
            uint32_t height = 0;
            uint32_t x = 0;
        };
        struct Page
        {
            std::vector<Shelf> shelves;
        };
        uint32_t _pageSize = 0;
        uint32_t _maxPages = 0;
        std::vector<Page> _pages;
    };

    // Byte budget. evict() drops the least recently used ids that are not pinned.
    class LruBudget
    {
    public:
        explicit LruBudget(uint32_t budgetBytes);
        void touch(uint32_t id, uint64_t frame);
        void insert(uint32_t id, uint32_t bytes, uint64_t frame);
        void erase(uint32_t id);
        std::vector<uint32_t> evict(uint32_t incomingBytes, const std::vector<uint32_t>& pinned);
        uint32_t used() const
        {
            return _used;
        }
        bool contains(uint32_t id) const;

    private:
        struct Item
        {
            uint32_t id = 0;
            uint32_t bytes = 0;
            uint64_t frame = 0;
        };
        uint32_t _budget = 0;
        uint32_t _used = 0;
        std::vector<Item> _items;
    };
} // namespace OpenRCT2::View3D
