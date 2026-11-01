/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "GpuBudget.hpp"

#include <algorithm>

namespace OpenRCT2::View3D
{
    GeometryArena::GeometryArena(uint32_t slabVertices, uint32_t slabIndices, uint32_t maxSlabs)
        : _slabVertices(slabVertices)
        , _slabIndices(slabIndices)
        , _maxSlabs(maxSlabs)
    {
    }

    bool GeometryArena::take(std::vector<Span>& spans, uint32_t count, uint32_t& offset)
    {
        for (size_t i = 0; i < spans.size(); i++)
        {
            if (spans[i].count < count)
            {
                continue;
            }
            offset = spans[i].offset;
            if (spans[i].count == count)
            {
                spans.erase(spans.begin() + static_cast<std::ptrdiff_t>(i));
            }
            else
            {
                spans[i].offset += count;
                spans[i].count -= count;
            }
            return true;
        }
        return false;
    }

    void GeometryArena::give(std::vector<Span>& spans, uint32_t offset, uint32_t count)
    {
        spans.push_back(Span{ offset, count });
        std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.offset < b.offset; });
        for (size_t i = 1; i < spans.size();)
        {
            if (spans[i - 1].offset + spans[i - 1].count == spans[i].offset)
            {
                spans[i - 1].count += spans[i].count;
                spans.erase(spans.begin() + static_cast<std::ptrdiff_t>(i));
            }
            else
            {
                i++;
            }
        }
    }

    bool GeometryArena::alloc(uint32_t vertices, uint32_t indices, BufferRange& vertexRange, BufferRange& indexRange)
    {
        if (vertices == 0 || indices == 0 || vertices > _slabVertices || indices > _slabIndices)
        {
            return false;
        }
        for (uint32_t slab = 0; slab < static_cast<uint32_t>(_slabs.size()); slab++)
        {
            uint32_t vertexOffset = 0;
            if (!take(_slabs[slab].vertices, vertices, vertexOffset))
            {
                continue;
            }
            uint32_t indexOffset = 0;
            if (!take(_slabs[slab].indices, indices, indexOffset))
            {
                give(_slabs[slab].vertices, vertexOffset, vertices);
                continue;
            }
            vertexRange = { slab, vertexOffset, vertices };
            indexRange = { slab, indexOffset, indices };
            return true;
        }
        if (_slabs.size() >= _maxSlabs)
        {
            return false;
        }
        Slab created;
        created.vertices.push_back(Span{ 0, _slabVertices });
        created.indices.push_back(Span{ 0, _slabIndices });
        _slabs.push_back(std::move(created));
        return alloc(vertices, indices, vertexRange, indexRange);
    }

    void GeometryArena::free(const BufferRange& vertexRange, const BufferRange& indexRange)
    {
        if (vertexRange.slab >= _slabs.size() || indexRange.slab >= _slabs.size())
        {
            return;
        }
        give(_slabs[vertexRange.slab].vertices, vertexRange.offset, vertexRange.count);
        give(_slabs[indexRange.slab].indices, indexRange.offset, indexRange.count);
    }

    AtlasPacker::AtlasPacker(uint32_t pageSize, uint32_t maxPages)
        : _pageSize(pageSize)
        , _maxPages(maxPages)
    {
    }

    bool AtlasPacker::pack(uint32_t width, uint32_t height, AtlasSlot& slot)
    {
        if (width == 0 || height == 0 || width > _pageSize || height > _pageSize)
        {
            return false;
        }
        for (uint32_t page = 0; page < static_cast<uint32_t>(_pages.size()); page++)
        {
            for (Shelf& shelf : _pages[page].shelves)
            {
                if (shelf.height < height || shelf.x + width > _pageSize)
                {
                    continue;
                }
                slot = { page, shelf.x, shelf.y, width, height };
                shelf.x += width;
                return true;
            }
            uint32_t y = 0;
            if (!_pages[page].shelves.empty())
            {
                const Shelf& last = _pages[page].shelves.back();
                y = last.y + last.height;
            }
            if (y + height > _pageSize)
            {
                continue;
            }
            _pages[page].shelves.push_back(Shelf{ y, height, width });
            slot = { page, 0, y, width, height };
            return true;
        }
        if (_pages.size() >= _maxPages)
        {
            return false;
        }
        _pages.push_back(Page{});
        return pack(width, height, slot);
    }

    LruBudget::LruBudget(uint32_t budgetBytes)
        : _budget(budgetBytes)
    {
    }

    bool LruBudget::contains(uint32_t id) const
    {
        for (const Item& item : _items)
        {
            if (item.id == id)
            {
                return true;
            }
        }
        return false;
    }

    void LruBudget::touch(uint32_t id, uint64_t frame)
    {
        for (Item& item : _items)
        {
            if (item.id == id)
            {
                item.frame = frame;
                return;
            }
        }
    }

    void LruBudget::insert(uint32_t id, uint32_t bytes, uint64_t frame)
    {
        erase(id);
        _items.push_back(Item{ id, bytes, frame });
        _used += bytes;
    }

    void LruBudget::erase(uint32_t id)
    {
        for (size_t i = 0; i < _items.size(); i++)
        {
            if (_items[i].id != id)
            {
                continue;
            }
            _used -= _items[i].bytes;
            _items.erase(_items.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }

    std::vector<uint32_t> LruBudget::evict(uint32_t incomingBytes, const std::vector<uint32_t>& pinned)
    {
        std::vector<uint32_t> dropped;
        const auto pinnedHas = [&](uint32_t id) { return std::find(pinned.begin(), pinned.end(), id) != pinned.end(); };
        while (_used + incomingBytes > _budget)
        {
            size_t oldest = _items.size();
            for (size_t i = 0; i < _items.size(); i++)
            {
                if (pinnedHas(_items[i].id))
                {
                    continue;
                }
                if (oldest == _items.size() || _items[i].frame < _items[oldest].frame)
                {
                    oldest = i;
                }
            }
            if (oldest == _items.size())
            {
                break;
            }
            dropped.push_back(_items[oldest].id);
            erase(_items[oldest].id);
        }
        return dropped;
    }
} // namespace OpenRCT2::View3D
