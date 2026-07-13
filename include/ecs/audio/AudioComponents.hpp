#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <entt/entt.hpp>

namespace gld::ecs {

    struct AudioCaptureRequest {
        std::string process_name;
        bool include_process_tree = true;
        float max_buffer_seconds = 2.0f;
        float publish_hz = 60.0f;
    };

    struct PcmAudio {
        std::uint32_t process_id = 0;
        std::string process_name;
        std::uint32_t sample_rate = 0;
        std::uint16_t channels = 0;
        std::vector<float> samples;
        std::uint64_t sequence = 0;
    };

    struct NormalizedPcmAudio {
        std::uint32_t process_id = 0;
        std::string process_name;
        std::uint32_t sample_rate = 0;
        std::uint16_t channels = 0;
        std::vector<float> samples;
        std::uint64_t source_sequence = 0;
        std::uint64_t sequence = 0;
        float gain = 1.0f;
        float measured_rms = 0.0f;
        float measured_peak = 0.0f;
    };

    struct FrequencyVolume {
        float frequency_hz = 0.f;
        float volume = 0.f;
    };

    enum class FftChannelLayout {
        Interleaved,
        Planar
    };

    struct FftSettings {
        std::uint32_t fft_size = 2048;
        bool multi_channel = false;
        FftChannelLayout channel_layout = FftChannelLayout::Interleaved;
    };

    struct FftSpectrum {
        std::uint32_t process_id = 0;
        std::string process_name;
        std::uint32_t sample_rate = 0;
        std::uint16_t channels = 0;
        std::uint32_t fft_size = 0;
        FftChannelLayout channel_layout = FftChannelLayout::Interleaved;
        std::vector<FrequencyVolume> bins;
        std::uint64_t source_sequence = 0;
    };

    struct NormalizedFftSpectrum {
        std::uint32_t process_id = 0;
        std::string process_name;
        std::uint32_t sample_rate = 0;
        std::uint16_t channels = 0;
        std::uint32_t fft_size = 0;
        FftChannelLayout channel_layout = FftChannelLayout::Interleaved;
        std::vector<FrequencyVolume> bins;
        std::uint64_t source_sequence = 0;
        float gain = 1.0f;
        float measured_level = 0.0f;
    };

    struct FftNormalizeSettings {
        bool enabled = true;
        float target_level = 0.18f;
        float attack = 0.35f;
        float release = 0.06f;
        float min_gain = 0.25f;
        float max_gain = 20.0f;
        float noise_floor = 0.00001f;
    };

    struct FftNormalizeState {
        struct Entry {
            float gain = 1.0f;
            std::uint64_t source_sequence = 0;
        };
        std::vector<std::pair<entt::entity, Entry>> entries;
    };

    struct PcmNormalizeSettings {
        bool enabled = true;
        float target_rms = 0.12f;
        float attack = 0.35f;
        float release = 0.05f;
        float min_gain = 0.2f;
        float max_gain = 8.0f;
        float noise_floor = 0.0005f;
        float limiter_peak = 0.98f;
    };

    struct PcmNormalizeState {
        struct Entry {
            float gain = 1.0f;
            std::uint64_t sequence = 0;
        };
        std::vector<std::pair<entt::entity, Entry>> entries;
    };
}
