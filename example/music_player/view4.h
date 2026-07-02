#pragma once

// View4 – "Honeycomb Terrain" (hex-grid audio spectrum terrain)
//
//  Point generation is NOT random anymore:
//   1. Build an abstract concentric-ring HONEYCOMB grid (center + R rings).
//      It only defines where points may live; nothing is drawn for it.
//   2. Each hex cell has an inscribed circle (apothem). Scaled per-axis by
//      (scalex, scaley, scalez) it becomes an ellipsoid = the random bound.
//      Final point = cell center + a random offset inside that ellipsoid
//      (z component → base height jitter, breaks the single layer).
//   3. Connectivity follows the honeycomb: every interior point links to its
//      6 hex neighbours (edge points fewer). Fixed at init; motion never
//      changes topology — each frame only rebuilds line vertices.
//   4. Frequency mapping is REGULAR by ring order: center = low freq, outward
//      rings = higher freq → a low-freq central mound and high-freq rings.
//
//  Kept from before: tilted terrain (tilt_deg ≤ 30), amplitude → height spike
//  (log1p compressed), HDR emissive colours + bloom, bloom-burst, bigger
//  points/lines, gentle drift, BiMeshComp dual draw (psize_vs + glow_fg).

#include <generator/Generator.hpp>
#include "fft_view.h"
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <random>
#include <vector>
#include <algorithm>
#include <set>
#include <utility>
#include <unordered_map>

namespace gld {

struct View4 : public FFTView {

    struct PointVertex { glm::vec3 pos; glm::vec3 color; float size; };
    struct LineVertex  { glm::vec3 pos; glm::vec3 color; };

    // -----------------------------------------------------------------------
    struct BiMeshComp : public Component {
        std::shared_ptr<gld::VertexArr> pt_vao;
        std::shared_ptr<gld::VertexArr> ln_vao;
        int pt_count = 0;
        int ln_count = 0;
        int uIsPoint_loc = -1;

        void draw() override {
            if (pt_vao && pt_count > 0) {
                if (uIsPoint_loc >= 0) glUniform1i(uIsPoint_loc, 1);
                pt_vao->bind();
                glDrawArrays(GL_POINTS, 0, pt_count);
                pt_vao->unbind();
            }
            if (ln_vao && ln_count > 0) {
                if (uIsPoint_loc >= 0) glUniform1i(uIsPoint_loc, 0);
                ln_vao->bind();
                glDrawArrays(GL_LINES, 0, ln_count);
                ln_vao->unbind();
            }
        }
        int64_t idx() override { return 100; }
    };

    // =======================================================================
    //  Easy-to-tweak parameters
    // =======================================================================
    int   hex_rings     = 13;     // concentric rings → N = 1 + 3R(R+1) points
    float X_RANGE       = 2.3f;   // half width  (fills viewport horizontally)
    float V_RANGE       = 1.5f;   // half height (fills viewport vertically)

    float scalex        = 0.75f;  // jitter ellipsoid scale of the incircle (u)
    float scaley        = 0.75f;  // jitter ellipsoid scale (v)
    float scalez        = 0.55f;  // jitter ellipsoid scale (height/depth)

    float tilt_deg      = 116.f;   // terrain tilt toward camera (0..30)

    float max_amp       = 0.9f;
    float comp_k        = 16.f;   // log1p compression strength
    float height_gain   = 0.9f;   // amplitude → height spike
    float orbit_h       = 0.04f;  // gentle vertical bob

    float bright_gain   = 2.2f;

    float base_size     = 9.0f;
    float size_gain     = 26.f;
    float line_width_px = 1.0f;
    float line_gain     = 0.75f;

    int   BLOOM_FRAMES  = 45;

    // -----------------------------------------------------------------------
    View4() = default;

    void create()
    {
        init_data();
        add_comp(std::make_shared<Transform>());

        auto render = std::shared_ptr<Render>(
            new Render("base/psize_vs.glsl", "base/glow_fg.glsl"));
        render->init();
        auto prog = render->get();
        if (prog->uniform_id("perspective") == -1)
            prog->locat_uniforms("perspective", "world", "model", "uIsPoint");
        add_comp<Render>(render);

        bi = std::make_shared<BiMeshComp>();
        bi->uIsPoint_loc = prog->uniform_id("uIsPoint");
        create_vaos();
        add_comp<BiMeshComp>(bi);
    }

    // -----------------------------------------------------------------------
    static glm::vec3 hsv2rgb(float h, float s, float v)
    {
        float c = v * s;
        float x = c * (1.f - std::fabs(std::fmod(h * 6.f, 2.f) - 1.f));
        float m = v - c;
        glm::vec3 rgb(0.f);
        switch (static_cast<int>(h * 6.f) % 6) {
            case 0: rgb = {c,x,0}; break; case 1: rgb = {x,c,0}; break;
            case 2: rgb = {0,c,x}; break; case 3: rgb = {0,x,c}; break;
            case 4: rgb = {x,0,c}; break; default: rgb = {c,0,x}; break;
        }
        return rgb + glm::vec3(m);
    }

    // Tilt a facing-camera plane back about X; height pushes toward camera.
    glm::vec3 tilt_apply(float u, float v, float H) const
    {
        float th = glm::radians(tilt_deg);
        float ct = std::cos(th), st = std::sin(th);
        float y = v * ct + H * st;
        float z = v * st - H * ct;
        return glm::vec3(u, y, z);
    }

    // -----------------------------------------------------------------------
    void on_update(float* data, int len) override
    {
        int N = (int)pts.size();
        float inv = (N > 1) ? 1.f / (float)(N - 1) : 0.f;

        if (bloom_frame < BLOOM_FRAMES) {
            float t = (float)bloom_frame / (float)BLOOM_FRAMES;
            bloom_t = 1.f - std::pow(1.f - t, 3.f);
            ++bloom_frame;
        } else {
            bloom_t = 1.f;
        }
        time_ += 0.016f;

        for (int i = 0; i < N; ++i) {
            Pt& p = pts[i];

            // Regular ring-order frequency mapping: center low, outer high.
            int bin = (len > 0)
                ? (int)std::lround((float)p.order * inv * (float)(len - 1)) : 0;
            if (bin < 0) bin = 0; else if (bin > len - 1) bin = len - 1;

            float raw = std::log1p(std::sqrt(std::max(0.f, data[bin])) * comp_k)
                      / std::log1p(comp_k) * max_amp;
            p.amp = (raw > p.amp) ? raw : p.amp * 0.80f + raw * 0.20f;

            float a1 = time_ * p.drift_speed + p.phase;
            float a2 = time_ * p.drift_speed * 1.3f + p.phase2;
            float a3 = time_ * p.drift_speed * 0.7f + p.phase3;
            float du = std::cos(a1) * p.orbit_r;
            float dv = std::sin(a2) * p.orbit_r;
            float dh = std::sin(a3) * orbit_h;

            float amp_norm = p.amp / max_amp;
            float H = p.baseH + dh + p.amp * height_gain;

            glm::vec3 world = tilt_apply(p.anchor.x + du, p.anchor.y + dv, H);
            p.pos = world * bloom_t;

            p.bright = 0.30f + amp_norm * bright_gain;
            p.size   = base_size + amp_norm * size_gain;
        }

        rebuild_points(N);
        rebuild_lines();
        upload_vaos();
    }

    size_t fft_data_length() override { return BASS_DATA_FFT1024; }

    void onDraw() override {
        glEnable(GL_PROGRAM_POINT_SIZE);
        glLineWidth(line_width_px);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDepthMask(GL_FALSE);
    }
    void onAfterDraw() override {
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glDisable(GL_PROGRAM_POINT_SIZE);
    }

private:
    struct Pt {
        glm::vec2 anchor;     // jittered (u,v) in terrain-local plane
        float baseH;          // base height (z jitter)
        glm::vec3 pos;
        glm::vec3 base_col;
        float hue;
        int   order;          // ring order → frequency bin
        float orbit_r;
        float phase, phase2, phase3;
        float drift_speed;
        float amp    = 0.f;
        float bright = 0.f;
        float size   = 5.f;
    };

    // -----------------------------------------------------------------------
    void init_data()
    {
        // ---- 1. concentric-ring honeycomb (axial coords, ring-major order) --
        static const int DQ[6] = { +1, +1,  0, -1, -1,  0 };
        static const int DR[6] = {  0, -1, -1,  0, +1, +1 };

        std::vector<glm::ivec2> ax;   // (q, r)
        ax.push_back({ 0, 0 });
        for (int k = 1; k <= hex_rings; ++k) {
            int q = -k, r = k;        // start = direction[4] * k = (-1,+1)*k
            for (int side = 0; side < 6; ++side) {
                for (int s = 0; s < k; ++s) {
                    ax.push_back({ q, r });
                    q += DQ[side]; r += DR[side];
                }
            }
        }
        int N = (int)ax.size();

        // ---- 2. axial → plane, auto-fit to viewport ------------------------
        const float S3 = std::sqrt(3.f);
        std::vector<glm::vec2> cc(N);
        float maxx = 1e-6f, maxy = 1e-6f;
        for (int i = 0; i < N; ++i) {
            float q = (float)ax[i].x, r = (float)ax[i].y;
            float cx = S3 * q + S3 * 0.5f * r;
            float cy = 1.5f * r;
            cc[i] = { cx, cy };
            maxx = std::max(maxx, std::fabs(cx));
            maxy = std::max(maxy, std::fabs(cy));
        }
        float sx = X_RANGE / maxx, sy = V_RANGE / maxy;

        // ---- 3. jitter each point inside the inscribed-circle ellipsoid ----
        float apothem = S3 * 0.5f;                 // incircle radius (norm)
        float rin_x = apothem * sx * scalex;
        float rin_y = apothem * sy * scaley;
        float rin_z = apothem * 0.5f * (sx + sy) * scalez;

        pts.resize(N);
        std::mt19937 rng(2024);
        std::uniform_real_distribution<float> u11(-1.f, 1.f);
        std::uniform_real_distribution<float> uphase(0.f, glm::two_pi<float>());
        std::uniform_real_distribution<float> uorbit(0.010f, 0.045f);
        std::uniform_real_distribution<float> uspeed(0.20f, 0.75f);

        for (int i = 0; i < N; ++i) {
            Pt& p = pts[i];

            glm::vec3 off;                          // random inside unit sphere
            do { off = glm::vec3(u11(rng), u11(rng), u11(rng)); }
            while (glm::dot(off, off) > 1.f);

            glm::vec2 center(cc[i].x * sx, cc[i].y * sy);
            p.anchor = center + glm::vec2(off.x * rin_x, off.y * rin_y);
            p.baseH  = off.z * rin_z;

            p.order   = N - 1 - i;                          // ring-major → freq order
            float t   = (N > 1) ? (float)i / (float)(N - 1) : 0.f;
            p.hue     = t;
            p.base_col= hsv2rgb(t, 0.85f, 1.f);

            p.orbit_r = uorbit(rng);
            p.phase   = uphase(rng);
            p.phase2  = uphase(rng);
            p.phase3  = uphase(rng);
            p.drift_speed = uspeed(rng);
            p.pos     = glm::vec3(0.f);
        }

        // ---- 4. fixed 6-neighbour edges (hex adjacency) --------------------
        std::unordered_map<long long, int> idx;
        auto key = [](int q, int r) {
            return (long long)(q + 100000) * 1000000LL + (long long)(r + 100000);
        };
        for (int i = 0; i < N; ++i) idx[key(ax[i].x, ax[i].y)] = i;

        std::set<std::pair<int,int>> uniq;
        for (int i = 0; i < N; ++i) {
            for (int d = 0; d < 6; ++d) {
                auto it = idx.find(key(ax[i].x + DQ[d], ax[i].y + DR[d]));
                if (it != idx.end()) {
                    int j = it->second;
                    uniq.insert({ std::min(i, j), std::max(i, j) });
                }
            }
        }
        edges.assign(uniq.begin(), uniq.end());

        pt_verts.assign(N, {});
        ln_verts.assign(edges.size() * 2, {});
        rebuild_points(N);
        rebuild_lines();
    }

    // -----------------------------------------------------------------------
    void rebuild_points(int n)
    {
        for (int i = 0; i < n; ++i) {
            const Pt& p = pts[i];
            pt_verts[i] = { p.pos, p.base_col * p.bright, p.size };
        }
    }

    void rebuild_lines()
    {
        for (size_t e = 0; e < edges.size(); ++e) {
            const Pt& a = pts[edges[e].first];
            const Pt& b = pts[edges[e].second];
            glm::vec3 ca = a.base_col * (a.bright * line_gain);
            glm::vec3 cb = b.base_col * (b.bright * line_gain);
            ln_verts[e * 2 + 0] = { a.pos, ca };
            ln_verts[e * 2 + 1] = { b.pos, cb };
        }
    }

    // -----------------------------------------------------------------------
    void upload_pts(std::shared_ptr<gld::VertexArr>& vao)
    {
        vao->bind();
        vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(pt_verts, GL_DYNAMIC_DRAW);
        vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
            gld::VAP_DATA<3, float, false>,
            gld::VAP_DATA<3, float, false>,
            gld::VAP_DATA<1, float, false>>();
        vao->unbind();
    }

    void upload_lns(std::shared_ptr<gld::VertexArr>& vao)
    {
        vao->bind();
        vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(ln_verts, GL_DYNAMIC_DRAW);
        vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
            gld::VAP_DATA<3, float, false>,
            gld::VAP_DATA<3, float, false>>();
        vao->unbind();
    }

    void create_vaos()
    {
        bi->pt_vao = std::make_shared<gld::VertexArr>();
        bi->pt_vao->create();
        bi->pt_vao->create_arr<gld::ArrayBufferType::VERTEX>();
        upload_pts(bi->pt_vao);
        bi->pt_count = (int)pt_verts.size();

        bi->ln_vao = std::make_shared<gld::VertexArr>();
        bi->ln_vao->create();
        bi->ln_vao->create_arr<gld::ArrayBufferType::VERTEX>();
        upload_lns(bi->ln_vao);
        bi->ln_count = (int)ln_verts.size();
    }

    void upload_vaos()
    {
        upload_pts(bi->pt_vao);
        bi->pt_count = (int)pt_verts.size();
        upload_lns(bi->ln_vao);
        bi->ln_count = (int)ln_verts.size();
    }

    // -----------------------------------------------------------------------
    float bloom_t     = 0.f;
    int   bloom_frame = 0;
    float time_       = 0.f;

    std::vector<Pt>                 pts;
    std::vector<std::pair<int,int>> edges;   // FIXED hex topology
    std::vector<PointVertex>        pt_verts;
    std::vector<LineVertex>         ln_verts;
    std::shared_ptr<BiMeshComp>     bi;
};

} // namespace gld
