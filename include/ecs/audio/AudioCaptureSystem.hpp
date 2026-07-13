#pragma once

#include <chrono>
#include <memory>
#include <cstdint>
#include <string>

#include "AudioComponents.hpp"
#include "../App.hpp"
#include "../EcsWorld.hpp"

namespace gld::ecs {

    struct AudioCaptureRuntime {
        std::shared_ptr<void> state;
        entt::entity output_entity = entt::null;
        std::string active_process_name;
        std::uint32_t active_process_id = 0;
        std::uint64_t last_sequence = 0;
        std::chrono::steady_clock::time_point last_publish_time{};
        std::string last_error;
    };

    void audio_capture_system(EcsWorld& w);
    void AudioCapturePlugin(App& app);
}
