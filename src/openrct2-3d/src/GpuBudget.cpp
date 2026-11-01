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

    AtlasPacker::AtlasPacker(uint32_t pageSize, uint32_t maxPages, uint32_t gutter)
        : _pageSize(pageSize)
        , _maxPages(maxPages)
        , _gutter(gutter)
    {
    }

    bool AtlasPacker::pack(uint32_t width, uint32_t height, AtlasSlot& slot)
    {
        const uint32_t needW = width + _gutter * 2u;
        const uint32_t needH = height + _gutter * 2u;
        if (width == 0 || height == 0 || needW > _pageSize || needH > _pageSize)
        {
            return false;
        }
        for (uint32_t page = 0; page < static_cast<uint32_t>(_pages.size()); page++)
        {
            std::vector<FreeRect>& freeRects = _pages[page].freeRects;
            for (size_t i = 0; i < freeRects.size(); i++)
            {
                const FreeRect rect = freeRects[i];
                if (rect.w < needW || rect.h < needH)
                {
                    continue;
                }
                freeRects.erase(freeRects.begin() + static_cast<std::ptrdiff_t>(i));
                if (rect.w > needW)
                {
                    freeRects.push_back(FreeRect{ rect.x + needW, rect.y, rect.w - needW, needH });
                }
                if (rect.h > needH)
                {
                    freeRects.push_back(FreeRect{ rect.x, rect.y + needH, rect.w, rect.h - needH });
                }
                slot = { page, rect.x + _gutter, rect.y + _gutter, width, height };
                return true;
            }
        }
        if (_pages.size() >= _maxPages)
        {
            return false;
        }
        Page created;
        created.freeRects.push_back(FreeRect{ 0, 0, _pageSize, _pageSize });
        _pages.push_back(std::move(created));
        return pack(width, height, slot);
    }

    void AtlasPacker::free(const AtlasSlot& slot)
    {
        if (slot.page >= _pages.size() || slot.width == 0 || slot.height == 0 || slot.x < _gutter || slot.y < _gutter)
        {
            return;
        }
        _pages[slot.page].freeRects.push_back(
            FreeRect{ slot.x - _gutter, slot.y - _gutter, slot.width + _gutter * 2u, slot.height + _gutter * 2u });
    }

    LruBudget::LruBudget(uint32_t budgetBytes)
        : _budget(budgetBytes)
    {
    }

    bool LruBudget::contains(uint32_t id) const
    {
        return _index.find(id) != _index.end();
    }

    void LruBudget::touch(uint32_t id, uint64_t frame)
    {
        const auto found = _index.find(id);
        if (found == _index.end())
        {
            return;
        }
        found->second->frame = frame;
        _order.splice(_order.begin(), _order, found->second);
    }

    void LruBudget::insert(uint32_t id, uint32_t bytes, uint64_t frame)
    {
        erase(id);
        _order.push_front(Item{ id, bytes, frame });
        _index.emplace(id, _order.begin());
        _used += bytes;
    }

    void LruBudget::erase(uint32_t id)
    {
        const auto found = _index.find(id);
        if (found == _index.end())
        {
            return;
        }
        _used -= found->second->bytes;
        _order.erase(found->second);
        _index.erase(found);
    }

    std::vector<uint32_t> LruBudget::evict(uint32_t incomingBytes, const std::unordered_set<uint32_t>& pinned)
    {
        std::vector<uint32_t> dropped;
        while (_used + incomingBytes > _budget)
        {
            auto it = _order.end();
            bool found = false;
            while (it != _order.begin())
            {
                --it;
                if (pinned.find(it->id) == pinned.end())
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                break;
            }
            const uint32_t id = it->id;
            dropped.push_back(id);
            erase(id);
        }
        return dropped;
    }

    std::optional<uint32_t> LruBudget::evictOne(const std::unordered_set<uint32_t>& pinned)
    {
        auto it = _order.end();
        while (it != _order.begin())
        {
            --it;
            if (pinned.find(it->id) != pinned.end())
            {
                continue;
            }
            const uint32_t id = it->id;
            erase(id);
            return id;
        }
        return std::nullopt;
    }
} // namespace OpenRCT2::View3D
