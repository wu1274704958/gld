#include <ecs\audio\FftSystem.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

namespace gld::ecs {

    namespace {
        constexpr float kPi = 3.14159265358979323846f;

        bool is_power_of_two(std::uint32_t v) {
            return v != 0 && (v & (v - 1)) == 0;
        }

        void fft_in_place(std::vector<std::complex<float>>& a) {
            const std::size_t n = a.size();
            for (std::size_t i = 1, j = 0; i < n; ++i) {
                std::size_t bit = n >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit;
                if (i < j) std::swap(a[i], a[j]);
            }

            for (std::size_t len = 2; len <= n; len <<= 1) {
                const float angle = -2.f * kPi / static_cast<float>(len);
                const std::complex<float> wlen(std::cos(angle), std::sin(angle));
                for (std::size_t i = 0; i < n; i += len) {
                    std::complex<float> w(1.f, 0.f);
                    const std::size_t half = len >> 1;
                    for (std::size_t j = 0; j < half; ++j) {
                        const std::complex<float> u = a[i + j];
                        const std::complex<float> v = a[i + j + half] * w;
                        a[i + j] = u + v;
                        a[i + j + half] = u - v;
                        w *= wlen;
                    }
                }
            }
        }

        std::vector<FrequencyVolume> compute_channel_bins(
            const PcmAudio& pcm,
            std::uint32_t fft_size,
            std::uint16_t source_channel) {

            std::vector<std::complex<float>> data(fft_size);
            const std::size_t channels = std::max<std::uint16_t>(pcm.channels, 1);
            const std::size_t frame_count = pcm.samples.size() / channels;
            const std::size_t start_frame = frame_count - fft_size;

            for (std::uint32_t i = 0; i < fft_size; ++i) {
                const float window = 0.5f * (1.f - std::cos(2.f * kPi * static_cast<float>(i) / static_cast<float>(fft_size - 1)));
                const std::size_t frame = start_frame + i;
                float sample = 0.f;
                if (source_channel == std::numeric_limits<std::uint16_t>::max()) {
                    for (std::size_t ch = 0; ch < channels; ++ch)
                        sample += pcm.samples[frame * channels + ch];
                    sample /= static_cast<float>(channels);
                } else {
                    sample = pcm.samples[frame * channels + source_channel];
                }
                data[i] = std::complex<float>(sample * window, 0.f);
            }

            fft_in_place(data);

            const std::uint32_t bin_count = fft_size / 2 + 1;
            std::vector<FrequencyVolume> bins;
            bins.reserve(bin_count);
            const float scale = 2.f / static_cast<float>(fft_size);
            for (std::uint32_t i = 0; i < bin_count; ++i) {
                bins.push_back(FrequencyVolume{
                    static_cast<float>(i) * static_cast<float>(pcm.sample_rate) / static_cast<float>(fft_size),
                    std::abs(data[i]) * scale
                });
            }
            return bins;
        }
    }

    void fft_system(EcsWorld& w) {
        auto& settings = w.resource_or_add<FftSettings>();
        if (!is_power_of_two(settings.fft_size) || settings.fft_size < 2) return;

        auto& reg = w.reg();
        std::vector<entt::entity> insufficient;

        for (auto e : reg.view<PcmAudio>()) {
            const auto& pcm = reg.get<PcmAudio>(e);
            if (pcm.channels == 0 || pcm.sample_rate == 0) {
                insufficient.push_back(e);
                continue;
            }

            const std::size_t frame_count = pcm.samples.size() / pcm.channels;
            if (frame_count < settings.fft_size) {
                insufficient.push_back(e);
                continue;
            }

            if (auto* existing = reg.try_get<FftSpectrum>(e);
                existing &&
                existing->source_sequence == pcm.sequence &&
                existing->fft_size == settings.fft_size &&
                existing->channel_layout == settings.channel_layout &&
                existing->channels == (settings.multi_channel ? pcm.channels : 1)) {
                continue;
            }

            FftSpectrum spectrum;
            spectrum.process_id = pcm.process_id;
            spectrum.process_name = pcm.process_name;
            spectrum.sample_rate = pcm.sample_rate;
            spectrum.fft_size = settings.fft_size;
            spectrum.channel_layout = settings.channel_layout;
            spectrum.source_sequence = pcm.sequence;

            if (!settings.multi_channel) {
                spectrum.channels = 1;
                spectrum.bins = compute_channel_bins(pcm, settings.fft_size, std::numeric_limits<std::uint16_t>::max());
            } else {
                spectrum.channels = pcm.channels;
                const std::uint32_t bin_count = settings.fft_size / 2 + 1;
                std::vector<std::vector<FrequencyVolume>> per_channel;
                per_channel.reserve(pcm.channels);
                for (std::uint16_t ch = 0; ch < pcm.channels; ++ch)
                    per_channel.push_back(compute_channel_bins(pcm, settings.fft_size, ch));

                spectrum.bins.reserve(static_cast<std::size_t>(bin_count) * pcm.channels);
                if (settings.channel_layout == FftChannelLayout::Planar) {
                    for (const auto& channel_bins : per_channel)
                        spectrum.bins.insert(spectrum.bins.end(), channel_bins.begin(), channel_bins.end());
                } else {
                    for (std::uint32_t bin = 0; bin < bin_count; ++bin) {
                        for (std::uint16_t ch = 0; ch < pcm.channels; ++ch)
                            spectrum.bins.push_back(per_channel[ch][bin]);
                    }
                }
            }

            reg.emplace_or_replace<FftSpectrum>(e, std::move(spectrum));
        }

        for (auto e : insufficient) {
            if (reg.valid(e) && reg.all_of<FftSpectrum>(e))
                reg.remove<FftSpectrum>(e);
        }
    }

    void FftPlugin(App& app) {
        app.world.resource_or_add<FftSettings>();
        app.add_system(Stage::PostUpdate, fft_system);
    }
}
