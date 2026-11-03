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
#include <deque>
#include <iterator>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace OpenRCT2::View3D
{
    struct ModelLoadQueue::Slot
    {
        ModelLoadPhase phase = ModelLoadPhase::absent;
        std::optional<CpuMesh> mesh;
        std::string error;
        bool inReady = false;
        std::list<std::string>::iterator readyIt;
    };

    struct ModelLoadQueue::State
    {
        std::mutex mutex;
        std::condition_variable wake;
        std::unordered_map<std::string, Slot> slots;
        std::deque<std::string> queue;
        std::list<std::string> ready;
        std::deque<std::string> failed;
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
            || slot.phase == ModelLoadPhase::delivered || slot.phase == ModelLoadPhase::failed)
        {
            return;
        }
        slot.phase = ModelLoadPhase::queued;
        slot.mesh.reset();
        slot.error.clear();
        _state->queue.push_back(identifier);
        _state->wake.notify_one();
    }

    void ModelLoadQueue::requestAbsent(const std::vector<std::string>& identifiers)
    {
        std::lock_guard<std::mutex> guard(_state->mutex);
        bool added = false;
        for (const std::string& identifier : identifiers)
        {
            Slot& slot = _state->slots[identifier];
            if (slot.phase == ModelLoadPhase::queued || slot.phase == ModelLoadPhase::loading
                || slot.phase == ModelLoadPhase::ready || slot.phase == ModelLoadPhase::delivered
                || slot.phase == ModelLoadPhase::failed)
            {
                continue;
            }
            slot.phase = ModelLoadPhase::queued;
            slot.mesh.reset();
            slot.error.clear();
            _state->queue.push_back(identifier);
            added = true;
        }
        if (added)
        {
            _state->wake.notify_one();
        }
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
        while (!_state->ready.empty())
        {
            identifier = _state->ready.front();
            _state->ready.pop_front();
            const auto found = _state->slots.find(identifier);
            if (found == _state->slots.end() || found->second.phase != ModelLoadPhase::ready || !found->second.mesh.has_value())
            {
                continue;
            }
            found->second.inReady = false;
            found->second.phase = ModelLoadPhase::delivered;
            return std::move(found->second.mesh);
        }
        return std::nullopt;
    }

    std::optional<std::string> ModelLoadQueue::takeFailed()
    {
        std::lock_guard<std::mutex> guard(_state->mutex);
        while (!_state->failed.empty())
        {
            const std::string identifier = _state->failed.front();
            _state->failed.pop_front();
            const auto found = _state->slots.find(identifier);
            if (found == _state->slots.end() || found->second.phase != ModelLoadPhase::failed)
            {
                continue;
            }
            return identifier;
        }
        return std::nullopt;
    }

    void ModelLoadQueue::forget(const std::string& identifier)
    {
        std::lock_guard<std::mutex> guard(_state->mutex);
        const auto found = _state->slots.find(identifier);
        if (found == _state->slots.end())
        {
            return;
        }
        if (found->second.inReady)
        {
            _state->ready.erase(found->second.readyIt);
        }
        _state->slots.erase(found);
    }

    void ModelLoadQueue::requestStop()
    {
        std::lock_guard<std::mutex> guard(_state->mutex);
        _state->stop = true;
        _state->wake.notify_all();
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
            _state->queue.pop_front();
            _state->slots[identifier].phase = ModelLoadPhase::loading;
        }
        std::string error;
        std::optional<CpuMesh> mesh;
        try
        {
            mesh = _load(identifier, error);
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
        }
        catch (...)
        {
            error = "model load failed";
        }
        std::lock_guard<std::mutex> guard(_state->mutex);
        const auto found = _state->slots.find(identifier);
        if (found == _state->slots.end() || found->second.phase != ModelLoadPhase::loading)
        {
            return;
        }
        if (mesh.has_value())
        {
            found->second.mesh = std::move(mesh);
            found->second.phase = ModelLoadPhase::ready;
            _state->ready.push_back(identifier);
            found->second.readyIt = std::prev(_state->ready.end());
            found->second.inReady = true;
        }
        else
        {
            found->second.error = error;
            found->second.phase = ModelLoadPhase::failed;
            _state->failed.push_back(identifier);
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
                // Leave queued work behind. Shutdown must not drain the list.
                if (_state->stop)
                {
                    return;
                }
                identifier = _state->queue.front();
                _state->queue.pop_front();
                _state->slots[identifier].phase = ModelLoadPhase::loading;
            }
            std::string error;
            std::optional<CpuMesh> mesh;
            try
            {
                mesh = _load(identifier, error);
            }
            catch (const std::exception& ex)
            {
                error = ex.what();
            }
            catch (...)
            {
                error = "model load failed";
            }
            std::lock_guard<std::mutex> guard(_state->mutex);
            const auto found = _state->slots.find(identifier);
            if (found == _state->slots.end() || found->second.phase != ModelLoadPhase::loading)
            {
                continue;
            }
            if (mesh.has_value())
            {
                found->second.mesh = std::move(mesh);
                found->second.phase = ModelLoadPhase::ready;
                _state->ready.push_back(identifier);
                found->second.readyIt = std::prev(_state->ready.end());
                found->second.inReady = true;
            }
            else
            {
                found->second.error = error;
                found->second.phase = ModelLoadPhase::failed;
                _state->failed.push_back(identifier);
            }
        }
    }
} // namespace OpenRCT2::View3D
