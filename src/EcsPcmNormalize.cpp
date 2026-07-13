#include <ecs/audio/PcmNormalizeSystem.hpp>

#include <algorithm>
#include <cmath>

namespace gld::ecs {

    namespace {
        PcmNormalizeState::Entry& entry_for(PcmNormalizeState& state, entt::entity e) {
            for (auto& [entity, entry] : state.entries) {
                if (entity == e) return entry;
            }
            state.entries.push_back({ e, PcmNormalizeState::Entry{} });
            return state.entries.back().second;
        }

        std::pair<float, float> measure_rms_peak(const std::vector<float>& samples) {
            if (samples.empty()) return { 0.f, 0.f };
            double sum_sq = 0.0;
            float peak = 0.f;
            for (float s : samples) {
                sum_sq += static_cast<double>(s) * static_cast<double>(s);
                peak = std::max(peak, std::abs(s));
            }
            return {
                static_cast<float>(std::sqrt(sum_sq / static_cast<double>(samples.size()))),
                peak
            };
        }
    }

    void pcm_normalize_system(EcsWorld& w) {
        auto& settings = w.resource_or_add<PcmNormalizeSettings>();
        auto& state = w.resource_or_add<PcmNormalizeState>();
        auto& reg = w.reg();

        std::vector<entt::entity> alive;
        for (auto e : reg.view<PcmAudio>()) {
            alive.push_back(e);
            const auto& pcm = reg.get<PcmAudio>(e);

            if (auto* existing = reg.try_get<NormalizedPcmAudio>(e);
                existing && existing->source_sequence == pcm.sequence && settings.enabled) {
                continue;
            }

            auto& entry = entry_for(state, e);
            auto [rms, peak] = measure_rms_peak(pcm.samples);

            float gain = entry.gain;
            if (settings.enabled) {
                const float desired = std::clamp(
                    settings.target_rms / std::max(rms, settings.noise_floor),
                    settings.min_gain,
                    settings.max_gain);
                const float speed = desired < gain ? settings.attack : settings.release;
                gain += (desired - gain) * speed;
                gain = std::clamp(gain, settings.min_gain, settings.max_gain);

                if (peak > settings.noise_floor && peak * gain > settings.limiter_peak)
                    gain = std::min(gain, settings.limiter_peak / peak);
            } else {
                gain = 1.0f;
            }

            NormalizedPcmAudio out;
            out.process_id = pcm.process_id;
            out.process_name = pcm.process_name;
            out.sample_rate = pcm.sample_rate;
            out.channels = pcm.channels;
            out.source_sequence = pcm.sequence;
            out.sequence = entry.sequence + 1;
            out.gain = gain;
            out.measured_rms = rms;
            out.measured_peak = peak;
            out.samples.reserve(pcm.samples.size());
            for (float s : pcm.samples)
                out.samples.push_back(std::clamp(s * gain, -1.0f, 1.0f));

            entry.gain = gain;
            entry.sequence = out.sequence;
            reg.emplace_or_replace<NormalizedPcmAudio>(e, std::move(out));
        }

        for (auto e : reg.view<NormalizedPcmAudio>()) {
            if (std::find(alive.begin(), alive.end(), e) == alive.end())
                reg.remove<NormalizedPcmAudio>(e);
        }

        state.entries.erase(std::remove_if(state.entries.begin(), state.entries.end(),
            [&](const auto& item) {
                return !reg.valid(item.first) || !reg.all_of<PcmAudio>(item.first);
            }), state.entries.end());
    }

    void PcmNormalizePlugin(App& app) {
        app.world.resource_or_add<PcmNormalizeSettings>();
        app.world.resource_or_add<PcmNormalizeState>();
        app.add_system(Stage::PostUpdate, pcm_normalize_system);
    }
}
