// ecs_audio_fft - process loopback capture + retro 2D bloom spectrum.
#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm\glm.hpp>
#include <glm\gtc\matrix_transform.hpp>

#include <FindPath.hpp>
#include <program.hpp>
#include <resource_mgr.hpp>
#include <texture.hpp>

#include <ecs\App.hpp>
#include <ecs\Window.hpp>
#include <ecs\assets\AssetServer.hpp>
#include <ecs\assets\FileSystem.hpp>
#include <ecs\audio\AudioCaptureSystem.hpp>
#include <ecs\audio\FftSystem.hpp>
#include <ecs\render\Batch.hpp>
#include <ecs\render\BatchSystem.hpp>
#include <ecs\render\PostProcess.hpp>
#include <ecs\render\RenderComponents.hpp>
#include <ecs\render\RenderSystem.hpp>
#include <ecs\systems\TransformSystem.hpp>

using namespace gld::ecs;
namespace fs = std::filesystem;

namespace {
    constexpr std::uint32_t kSpectrumLayer = 0x1u;

    struct SpectrumVizState {
        entt::entity camera = entt::null;
        entt::entity batch = entt::null;
        Handle<gld::Program> shader;
        Handle<gld::Texture<gld::TexType::D2>> nixie_texture;
        std::vector<float> smooth;
        std::uint64_t last_sequence = 0;
        int max_bars = 84;
        int max_segments = 72;
        float nixie_aspect = 3.3708f;
        float segment_h = 3.0f;
        float gap_h = 3.5f;
        float gap_w = 3.0f;
        float min_hz = 35.f;
        float max_hz = 16000.f;
        std::vector<glm::vec4> colors{
            glm::vec4(1.00f, 0.20f, 0.02f, 1.f),
            glm::vec4(1.00f, 0.58f, 0.02f, 1.f),
            glm::vec4(0.02f, 0.95f, 1.00f, 1.f),
            glm::vec4(0.65f, 0.12f, 1.00f, 1.f),
        };
        float base_emissive = 1.25f;
        float bar_emissive_gain = 3.4f;
        float bar_emissive_power = 1.25f;
        float segment_emissive_gain = 1.15f;
        float segment_emissive_power = 1.4f;
        float top_segment_emissive = 1.35f;
    };

    glm::vec4 spectrum_color(const SpectrumVizState& state, int bar) {
        if (state.colors.empty()) return glm::vec4(1.f);
        const float t = static_cast<float>(bar) / static_cast<float>(std::max(state.max_bars - 1, 1));
        const int idx = std::clamp(
            static_cast<int>(t * static_cast<float>(state.colors.size())),
            0,
            static_cast<int>(state.colors.size()) - 1);
        return state.colors[static_cast<std::size_t>(idx)];
    }

    float segment_emissive(const SpectrumVizState& state, float bar_level, float segment_t, bool is_top) {
        const float bar_glow = 1.0f + state.bar_emissive_gain * std::pow(bar_level, state.bar_emissive_power);
        const float segment_glow = 1.0f + state.segment_emissive_gain * std::pow(segment_t, state.segment_emissive_power);
        const float top_glow = is_top ? state.top_segment_emissive : 1.0f;
        return state.base_emissive * bar_glow * segment_glow * top_glow;
    }

    float band_volume(const FftSpectrum& spectrum, int bar, int bars, float min_hz, float max_hz) {
        if (spectrum.bins.empty()) return 0.f;

        const float nyquist = static_cast<float>(spectrum.sample_rate) * 0.5f;
        const float hi = std::min(max_hz, nyquist);
        const float lo = std::max(min_hz, 1.f);
        const float t0 = static_cast<float>(bar) / static_cast<float>(bars);
        const float t1 = static_cast<float>(bar + 1) / static_cast<float>(bars);
        const float f0 = lo * std::pow(hi / lo, t0);
        const float f1 = lo * std::pow(hi / lo, t1);

        auto bin_for = [&](float hz) {
            const float bin = hz * static_cast<float>(spectrum.fft_size) / static_cast<float>(spectrum.sample_rate);
            return std::clamp(static_cast<int>(std::floor(bin)), 1, static_cast<int>(spectrum.bins.size()) - 1);
        };

        const int begin = bin_for(f0);
        const int end = std::clamp(bin_for(f1) + 1, begin + 1, static_cast<int>(spectrum.bins.size()));

        float sum = 0.f;
        float sum_sq = 0.f;
        float peak = 0.f;
        for (int i = begin; i < end; ++i) {
            const float v = spectrum.bins[static_cast<std::size_t>(i)].volume;
            sum += v;
            sum_sq += v * v;
            peak = std::max(peak, v);
        }
        const float n = static_cast<float>(end - begin);
        const float avg = sum / n;
        const float rms = std::sqrt(sum_sq / n);
        const float high_boost = 1.f + 1.35f * std::pow(t0, 1.2f);
        return (avg * 0.22f + rms * 0.38f + peak * 0.40f) * high_boost;
    }

    void push_segment(BatchComponent& batch,
                      float x,
                      float y,
                      float w,
                      float h,
                      glm::vec4 color,
                      float emissive) {
        InstanceData d;
        d.rect = glm::vec4(0.f, 0.f, 1.f, 1.f);
        d.pad = d.rect;
        d.color = color;
        d.transform = glm::translate(glm::mat4(1.f), glm::vec3(x, y, 0.f)) *
            glm::scale(glm::mat4(1.f), glm::vec3(w, h, 1.f));
        d.mparam0 = glm::vec4(emissive, 0.f, 0.f, 0.f);
        batch.instances.push_back(d);
    }

    void spectrum_visualizer_system(EcsWorld& w) {
        auto* state = w.try_resource<SpectrumVizState>();
        auto* win = w.try_resource<Window>();
        if (!state || !win) return;
        if (state->batch == entt::null || !w.reg().valid(state->batch)) return;

        const FftSpectrum* spectrum = nullptr;
        for (auto e : w.reg().view<FftSpectrum>()) {
            spectrum = &w.reg().get<FftSpectrum>(e);
            break;
        }

        auto& batch = w.reg().get<BatchComponent>(state->batch);
        batch.used = true;

        gld::Program* prog = state->shader.get();
        auto* tex = state->nixie_texture.get();
        if (!prog || !tex) {
            batch.instances.clear();
            batch.dirty = true;
            return;
        }
        batch.prog = prog;
        batch.key.shader = static_cast<unsigned int>(*prog);
        batch.key.atlas = tex->get_id();
        batch.layers = kSpectrumLayer;
        batch.key.layers = kSpectrumLayer;

        if (!spectrum) {
            batch.instances.clear();
            batch.dirty = true;
            return;
        }

        if (state->smooth.size() != static_cast<std::size_t>(state->max_bars))
            state->smooth.assign(static_cast<std::size_t>(state->max_bars), 0.f);

        const float width = static_cast<float>(win->width);
        const float height = static_cast<float>(win->height);
        const float margin_x = 34.f;
        const float bottom = -height * 0.5f + 34.f;
        const float available_w = std::max(0.f, width - margin_x * 2.f);
        const float bar_w = state->segment_h * state->nixie_aspect;
        const float bar_pitch = bar_w + state->gap_w;
        const int visible_bars = std::clamp(
            static_cast<int>(available_w / bar_pitch),
            1,
            std::min(state->max_bars, static_cast<int>(spectrum->bins.size())));
        const float segment_pitch = state->segment_h + state->gap_h;
        const int max_segments = std::clamp(
            static_cast<int>((height - 92.f) / segment_pitch),
            8,
            state->max_segments);

        batch.instances.clear();
        batch.instances.reserve(static_cast<std::size_t>(visible_bars * max_segments));

        for (int i = 0; i < visible_bars; ++i) {
            const float raw = band_volume(*spectrum, i, visible_bars, state->min_hz, state->max_hz);
            const float target = std::clamp(std::pow(std::log1p(raw * 420.f) / 4.0f, 0.62f), 0.f, 1.f);
            const float speed = target > state->smooth[static_cast<std::size_t>(i)] ? 0.78f : 0.18f;
            float& smoothed = state->smooth[static_cast<std::size_t>(i)];
            smoothed += (target - smoothed) * speed;

            int segments = std::clamp(static_cast<int>(std::ceil(smoothed * max_segments)), 0, max_segments);
            if (segments == 0 && target > 0.045f)
                segments = 1;
            const float x = -width * 0.5f + margin_x + bar_w * 0.5f + static_cast<float>(i) * bar_pitch;
            const glm::vec4 c = spectrum_color(*state, i);
            for (int s = 0; s < segments; ++s) {
                const float t = static_cast<float>(s + 1) / static_cast<float>(max_segments);
                const float y = bottom + state->segment_h * 0.5f + static_cast<float>(s) * segment_pitch;
                const float emissive = segment_emissive(*state, smoothed, t, s == segments - 1);
                push_segment(batch, x, y, bar_w, state->segment_h, c, emissive);
            }
        }

        batch.sig = spectrum->source_sequence;
        batch.count = batch.instances.size();
        batch.dirty = true;
        state->last_sequence = spectrum->source_sequence;
    }

    void diagnostics_system(EcsWorld& w) {
        static int frame = 0;
        if (++frame % 90 != 0) return;

        auto* runtime = w.try_resource<AudioCaptureRuntime>();
        if (runtime && !runtime->last_error.empty())
            std::printf("[ecs_audio_fft] capture error: %s\n", runtime->last_error.c_str());

        bool printed = false;
        auto& reg = w.reg();
        for (auto e : reg.view<PcmAudio>()) {
            const auto& pcm = reg.get<PcmAudio>(e);
            std::printf("[ecs_audio_fft] pid=%u name=%s pcm_samples=%zu rate=%u channels=%u seq=%llu",
                pcm.process_id,
                pcm.process_name.c_str(),
                pcm.samples.size(),
                pcm.sample_rate,
                pcm.channels,
                static_cast<unsigned long long>(pcm.sequence));

            if (auto* spectrum = reg.try_get<FftSpectrum>(e))
                std::printf(" fft_bins=%zu", spectrum->bins.size());
            std::printf("\n");
            printed = true;
        }

        if (!printed && runtime)
            std::printf("[ecs_audio_fft] waiting for process/audio: target=%s\n",
                runtime->active_process_name.empty() ? "(not found)" : runtime->active_process_name.c_str());
        std::fflush(stdout);
    }
}

int main(int argc, char** argv)
{
    const std::string process_name = argc > 1 ? argv[1] : "chrome.exe";

    fs::path root = wws::find_path(3, "res", true);
    gld::ResMgrWithGlslPreProcess::create_instance(root);
    gld::DefResMgr::create_instance(root);

    App app;
    app.add_plugin(WindowPlugin{ 1180, 520, "ecs_audio_fft - retro bloom spectrum" });
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root));
    app.add_plugin(AssetPlugin);
    app.add_plugin(CorePlugin);
    app.add_plugin(AudioCapturePlugin);
    app.add_plugin(FftPlugin);
    app.add_plugin(PostProcessPlugin);
    app.add_plugin(RenderPlugin);

    auto& req = app.world.resource_or_add<AudioCaptureRequest>();
    req.process_name = process_name;
    req.include_process_tree = true;
    req.max_buffer_seconds = 0.25f;
    req.publish_hz = 60.f;

    auto& fft = app.world.resource_or_add<FftSettings>();
    fft.fft_size = 2048;
    fft.multi_channel = false;
    fft.channel_layout = FftChannelLayout::Interleaved;

    app.add_system(Stage::Startup, [](EcsWorld& w) {
        auto& srv = w.resource<AssetServer>();
        auto& reg = w.reg();

        SpectrumVizState state;
        state.shader = srv.load_program("ecs/text_vs.glsl", "ecs/spectrum_bar_fg.glsl");
        state.nixie_texture = srv.load_texture("textures/nixie_tube.png", Channels::Gray);

        state.camera = w.spawn();
        Camera cam;
        cam.kind = CameraKind::Ortho;
        cam.priority = 0;
        cam.target = 0;
        cam.layers = kSpectrumLayer;
        cam.clear_color = glm::vec4(0.004f, 0.006f, 0.010f, 1.f);
        reg.emplace<Camera>(state.camera, cam);
        emplace_render_passes<BatchPass>(w, state.camera);

        state.batch = w.spawn();
        auto& batch = reg.emplace<BatchComponent>(state.batch);
        batch.layers = kSpectrumLayer;
        batch.key.layers = kSpectrumLayer;

        auto& ppm = w.resource<PostProcessManager>();
        ppm.add_post_process(state.camera, BloomPostProcessDesc{
            0.20f,
            0.38f,
            3.25f,
            1.05f
        });

        w.add_resource<SpectrumVizState>(std::move(state));
    });

    app.add_system(Stage::PostUpdate, spectrum_visualizer_system);
    app.add_system(Stage::Last, diagnostics_system);

    run_app(app);
    return 0;
}
