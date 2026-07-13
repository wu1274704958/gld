#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
}
