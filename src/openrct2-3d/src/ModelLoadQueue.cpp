/*****************************************************************************
 * Copyright (c) 2014-2026 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "ModelLoadQueue.hpp"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace OpenRCT2::View3D
{
    struct ModelLoadQueue::Slot
    {
        ModelLoadPhase phase = ModelLoadPhase::absent;
        std::optional<CpuMesh> mesh;
        std::string error;
    };

    struct ModelLoadQueue::State
    {
        std::mutex mutex;
        std::condition_variable wake;
        std::unordered_map<std::string, Slot> slots;
        std::vector<std::string> queue;
        bool stop = false;
        std::thread worker;
    };

    ModelLoadQueue::ModelLoadQueue(LoadFn load, bool startWorker)
        : _load(std::move(load))
        , _state(new State())
    {
        if (startWorker)
        {
            _state->worker = std::thread([this]() { worker(); });
        }
    }

    ModelLoadQueue::~ModelLoadQueue()
    {
        {
            std::lock_guard<std::mutex> guard(_state->mutex);
            _state->stop = true;
        }
        _state->wake.notify_all();
        if (_state->worker.joinable())
        {
            _state->worker.join();
        }
        delete _state;
    }

    void ModelLoadQueue::request(const std::string& identifier)
    {
        std::lock_guard<std::mutex> guard(_state->mutex);
        Slot& slot = _state->slots[identifier];
        if (slot.phase == ModelLoadPhase::queued || slot.phase == ModelLoadPhase::loading || slot.phase == ModelLoadPhase::ready
            || slot.phase == ModelLoadPhase::delivered)
        {
            return;
        }
        slot.phase = ModelLoadPhase::queued;
        slot.mesh.reset();
        slot.error.clear();
        _state->queue.push_back(identifier);
        _state->wake.notify_one();
    }

    ModelLoadPhase ModelLoadQueue::phase(const std::string& identifier) const
    {
        std::lock_guard<std::mutex> guard(_state->mutex);
        const auto found = _state->slots.find(identifier);
        if (found == _state->slots.end())
        {
            return ModelLoadPhase::absent;
        }
        return found->second.phase;
    }

    std::optional<CpuMesh> ModelLoadQueue::takeReady(std::string& identifier)
    {
        std::lock_guard<std::mutex> guard(_state->mutex);
        for (auto& entry : _state->slots)
        {
            if (entry.second.phase != ModelLoadPhase::ready || !entry.second.mesh.has_value())
            {
                continue;
            }
            identifier = entry.first;
            entry.second.phase = ModelLoadPhase::delivered;
            std::optional<CpuMesh> mesh = std::move(entry.second.mesh);
            return mesh;
        }
        return std::nullopt;
    }

    void ModelLoadQueue::forget(const std::string& identifier)
    {
        std::lock_guard<std::mutex> guard(_state->mutex);
        _state->slots.erase(identifier);
    }

    void ModelLoadQueue::pump()
    {
        std::string identifier;
        {
            std::lock_guard<std::mutex> guard(_state->mutex);
            if (_state->queue.empty())
            {
                return;
            }
            identifier = _state->queue.front();
            _state->queue.erase(_state->queue.begin());
            _state->slots[identifier].phase = ModelLoadPhase::loading;
        }
        std::string error;
        std::optional<CpuMesh> mesh = _load(identifier, error);
        std::lock_guard<std::mutex> guard(_state->mutex);
        Slot& slot = _state->slots[identifier];
        if (mesh.has_value())
        {
            slot.mesh = std::move(mesh);
            slot.phase = ModelLoadPhase::ready;
        }
        else
        {
            slot.error = error;
            slot.phase = ModelLoadPhase::failed;
        }
    }

    void ModelLoadQueue::worker()
    {
        for (;;)
        {
            std::string identifier;
            {
                std::unique_lock<std::mutex> guard(_state->mutex);
                _state->wake.wait(guard, [&]() { return _state->stop || !_state->queue.empty(); });
                if (_state->stop && _state->queue.empty())
                {
                    return;
                }
                identifier = _state->queue.front();
                _state->queue.erase(_state->queue.begin());
                _state->slots[identifier].phase = ModelLoadPhase::loading;
            }
            std::string error;
            std::optional<CpuMesh> mesh = _load(identifier, error);
            std::lock_guard<std::mutex> guard(_state->mutex);
            Slot& slot = _state->slots[identifier];
            if (mesh.has_value())
            {
                slot.mesh = std::move(mesh);
                slot.phase = ModelLoadPhase::ready;
            }
            else
            {
                slot.error = error;
                slot.phase = ModelLoadPhase::failed;
            }
        }
    }
} // namespace OpenRCT2::View3D
