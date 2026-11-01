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

#include <functional>
#include <optional>
#include <string>

namespace OpenRCT2::View3D
{
    enum class ModelLoadPhase
    {
        absent,
        queued,
        loading,
        ready,
        failed,
        delivered,
    };

    // Reads validated caches off the render thread. The loader must not touch GameState.
    // `pump` runs one job on the calling thread so tests do not need the worker.
    class ModelLoadQueue
    {
    public:
        using LoadFn = std::function<std::optional<CpuMesh>(const std::string& identifier, std::string& error)>;

        ModelLoadQueue(LoadFn load, bool startWorker);
        ~ModelLoadQueue();

        ModelLoadQueue(const ModelLoadQueue&) = delete;
        ModelLoadQueue& operator=(const ModelLoadQueue&) = delete;

        void request(const std::string& identifier);
        ModelLoadPhase phase(const std::string& identifier) const;
        std::optional<CpuMesh> takeReady(std::string& identifier);
        void forget(const std::string& identifier);
        void pump();

    private:
        void worker();
        struct Slot;
        LoadFn _load;
        struct State;
        State* _state = nullptr;
    };
} // namespace OpenRCT2::View3D
