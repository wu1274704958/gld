#pragma once

// View3 - "Aurora Crown"
//
//  Visual concept:
//   - 256 outer radial bars  : line from base_ring outward, length = amplitude
//   - 256 inner mirror bars  : same base_ring, shorter bar going inward (35 % of outer)
//   - Rainbow HSV color per bar (hue = angle), brightness reacts to amplitude
//   - Fast-attack / slow-decay amplitude smoothing (peak-hold feel)
//   - Whole display slowly auto-rotates on Y axis
//
//  Uses line3 shader (round-tube geometry shader with soft glow edges).

#include <generator/Generator.hpp>
#include "fft_view.h"
#include <cmath>
#include <algorithm>

namespace gld {

struct View3 : public FFTView {

    // -----------------------------------------------------------------------
    // Vertex layout identical to View1 / View2
    // -----------------------------------------------------------------------
    struct Vertex {
        glm::vec3 pos;
        glm::vec3 color;
    };

    View3() {}

    // -----------------------------------------------------------------------
    void create()
    {
        prepare_vertices();
        add_comp(std::make_shared<Transform>());

        // Plain solid-color shader — no geometry shader, no alpha gradient.
        // The line3 tube shader's semi-transparent edges were the moiré source.
        auto render = std::shared_ptr<Render>(
            new Render("base/bar_vs.glsl", "base/bar_fg.glsl"));
        render->init();
        auto program = render->get();
        if (program->uniform_id("perspective") == -1)
            program->locat_uniforms("perspective", "world", "model");
        add_comp<Render>(render);

        // no line_width uniform needed with the plain bar shader

        auto vao = std::make_shared<gld::VertexArr>();
        vao->create();
        vao->create_arr<gld::ArrayBufferType::VERTEX>();
        vao->bind();
        vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(vertices, GL_DYNAMIC_DRAW);
        vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
            gld::VAP_DATA<3, float, false>, gld::VAP_DATA<3, float, false>>();
        vao->unbind();

        auto mesh = std::shared_ptr<def::Mesh>(new def::Mesh(
            0, static_cast<int>(vertices.size()), std::move(vao)));
        mesh->mode = GL_LINES;
        add_comp<def::Mesh>(mesh);
    }

    // -----------------------------------------------------------------------
    // HSV → linear RGB (h in [0,1), s/v in [0,1])
    // -----------------------------------------------------------------------
    static glm::vec3 hsv2rgb(float h, float s, float v)
    {
        float c = v * s;
        float x = c * (1.f - std::fabs(std::fmod(h * 6.f, 2.f) - 1.f));
        float m = v - c;
        glm::vec3 rgb(0.f);
        switch (static_cast<int>(h * 6.f) % 6) {
            case 0: rgb = {c, x, 0}; break;
            case 1: rgb = {x, c, 0}; break;
            case 2: rgb = {0, c, x}; break;
            case 3: rgb = {0, x, c}; break;
            case 4: rgb = {x, 0, c}; break;
            default: rgb = {c, 0, x}; break;
        }
        return rgb + glm::vec3(m);
    }

    // -----------------------------------------------------------------------
    void prepare_vertices()
    {
        // 2 verts per outer bar + 2 per inner bar = 4 per bar
        vertices.resize(static_cast<size_t>(bar_count) * 4);
        smoothed.assign(bar_count, 0.f);
        peaks.assign(bar_count, 0.f);
        peak_hold.assign(bar_count, 0);

        for (int i = 0; i < bar_count; ++i) {
            float t   = (float)i / (float)bar_count;
            float ang = t * glm::two_pi<float>();
            glm::vec3 dir(std::cos(ang), 0.f, std::sin(ang));
            glm::vec3 col = hsv2rgb(t, 1.f, 0.45f);
            // outer bar (both endpoints at base, will grow during on_update)
            vertices[i * 2 + 0] = { dir * inner_r, col };
            vertices[i * 2 + 1] = { dir * inner_r, col };
            // inner bar
            vertices[bar_count * 2 + i * 2 + 0] = { dir * inner_r, col * 0.5f };
            vertices[bar_count * 2 + i * 2 + 1] = { dir * inner_r, col * 0.5f };
        }
    }

    // -----------------------------------------------------------------------
    void on_update(float* data, int len) override
    {
        rot_angle += rot_speed;
        if (rot_angle > glm::two_pi<float>()) rot_angle -= glm::two_pi<float>();

        // --- pass 1: accumulate FFT bins per bar ---------------------------------
        // Each bar averages (len / bar_count) consecutive FFT bins, which naturally
        // low-pass-filters the spectrum and eliminates the aliasing "beat" that
        // was creating the moiré when bar spacing ≈ line width.
        const int bins_per = std::max(1, len / bar_count);

        for (int i = 0; i < bar_count; ++i) {
            float t = (float)i / (float)bar_count;

            // Average multiple FFT bins into one bar value
            float sum = 0.f;
            int   cnt = 0;
            for (int b = 0; b < bins_per; ++b) {
                int bin = std::min(i * bins_per + b, len - 1);
                sum += data[bin];
                ++cnt;
            }
            float bin_avg = sum / (float)cnt;

            // Frequency-dependent gain: bass t≈0 → 0.10, treble t≈1 → 1.00
            float freq_weight = 0.10f + 0.90f * std::pow(t, 0.45f);

            float raw = std::sqrt(bin_avg) * amp_scale * freq_weight;

            // Fast-attack / slow-decay
            if (raw > smoothed[i])
                smoothed[i] = raw;
            else
                smoothed[i] = smoothed[i] * 0.85f + raw * 0.15f;
        }

        // --- pass 2: build geometry (tanh compress + peak-hold + geometry) ------
        for (int i = 0; i < bar_count; ++i) {
            float x   = smoothed[i];
            float amp = max_amp * std::tanh(x / max_amp);

            // Peak-hold
            if (amp >= peaks[i]) {
                peaks[i]    = amp;
                peak_hold[i] = 28;
            } else {
                if (peak_hold[i] > 0) {
                    --peak_hold[i];
                } else {
                    peaks[i] = std::max(0.f, peaks[i] - 0.01f);
                }
            }

            float t   = (float)i / (float)bar_count;
            float ang = t * glm::two_pi<float>() + rot_angle;
            glm::vec3 dir(std::cos(ang), 0.f, std::sin(ang));

            float bright   = 0.3f + (amp / max_amp) * 0.7f;
            float tip_hue  = std::fmod(t + 0.18f, 1.f);
            glm::vec3 base_col = hsv2rgb(t,       1.f,  bright);
            glm::vec3 tip_col  = hsv2rgb(tip_hue, 0.35f, 1.f);

            glm::vec3 tip_pos = dir * (inner_r + amp);
            if (peaks[i] > amp + 0.01f) {
                tip_pos = dir * (inner_r + peaks[i]);
                tip_col = hsv2rgb(tip_hue, 0.2f, 1.f);
            }

            vertices[i * 2 + 0] = { dir * inner_r, base_col };
            vertices[i * 2 + 1] = { tip_pos,         tip_col };

            float i_amp = std::min(amp * 0.35f, inner_r - 0.03f);
            glm::vec3 i_col = hsv2rgb(t, 0.8f, bright * 0.55f);
            vertices[bar_count * 2 + i * 2 + 0] = { dir * inner_r,          base_col * 0.45f };
            vertices[bar_count * 2 + i * 2 + 1] = { dir * (inner_r - i_amp), i_col           };
        }

        // Upload updated vertices
        auto& vao = get_comp<def::Mesh>()->vao;
        vao->bind();
        vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(vertices, GL_DYNAMIC_DRAW);
        vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
            gld::VAP_DATA<3, float, false>, gld::VAP_DATA<3, float, false>>();
        vao->unbind();
    }

    size_t fft_data_length() override { return BASS_DATA_FFT512; }

    void onDraw() override {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    void onAfterDraw() override {
        glDisable(GL_BLEND);
    }

    // -----------------------------------------------------------------------
    // Parameters (tweakable from outside)
    // -----------------------------------------------------------------------
    float inner_r   = 0.48f;
    float amp_scale = 1.4f;
    float max_amp   = 0.85f;
    float rot_speed = 0.0018f;
    int   bar_count = 256;     // restored — solid lines don't cause moiré

private:
    float rot_angle = 0.f;
    std::vector<Vertex> vertices;
    std::vector<float>  smoothed;     // smoothed amplitude per bar
    std::vector<float>  peaks;        // peak-hold amplitude per bar
    std::vector<int>    peak_hold;    // frames remaining for peak hold
};

} // namespace gld
