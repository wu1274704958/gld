#pragma once

// View5 – "Album Ripple Field" (cover-coloured concentric-ripple point cloud)
//
//  Point generation and frequency logic are DECOUPLED:
//   * generate_points(): a uniform-spacing honeycomb point cloud sized purely by
//     point_count + point_spacing (a ~circular cluster). No frequency coupling.
//   * assign_freq_rings(): a SEPARATE system of concentric circular rings by
//     actual radius (ring_start_radius, ring_count, ring_width). For each point
//     it finds which ring it falls in (→ freq_ring / frequency bin, center=high
//     freq, outer=low) and its normalised distance to the ring edge:
//         s = local*2-1 ∈ [-1,1];  dist = |s| ∈ [0,1]   (0 = ring middle)
//   * on_update(): applies the frequency. dist is mapped through an inverted
//     catenary (arch) so dist=0 → highest, dist→1 → lowest:
//         f = (cosh(k) - cosh(k*dist)) / (cosh(k) - 1)
//     then  Y_height = f * amplitude * height_gain  → concentric ripples.
//
//  Colour: sampled from the song's embedded album art (CoverArt) at the point's
//  relative UV; random noise if no cover. Kept: tilted terrain, HDR + bloom,
//  bloom-burst, amplitude→size, looping drift, BiMeshComp (points only).

#include <generator/Generator.hpp>
#include "fft_view.h"
#include "cover_art.h"
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <random>
#include <vector>

namespace gld {

struct View5 : public FFTView {

    struct PointVertex { glm::vec3 pos; glm::vec3 color; float size; };

    // -----------------------------------------------------------------------
    struct BiMeshComp : public Component {
        std::shared_ptr<gld::VertexArr> pt_vao;
        int pt_count = 0;
        int uIsPoint_loc = -1;

        void draw() override {
            if (pt_vao && pt_count > 0) {
                if (uIsPoint_loc >= 0) glUniform1i(uIsPoint_loc, 1);
                pt_vao->bind();
                glDrawArrays(GL_POINTS, 0, pt_count);
                pt_vao->unbind();
            }
        }
        int64_t idx() override { return 100; }
    };

    // =======================================================================
    //  Easy-to-tweak parameters
    // =======================================================================
    // --- point generation (independent of frequency) ---
    int   point_count    = 34000;  // target total number of points
    float point_spacing  = 0.012f;  // world distance between adjacent points
    float scalex         = 0.75f;  // inscribed-circle jitter ellipsoid scale (x)
    float scaley         = 0.75f;  // (y)
    float scalez         = 0.55f;  // (z → random base height)
    float spin_speed     = 0.1f;   // parent transform Y-axis spin (radians/sec)
    glm::vec3 spin_scale = glm::vec3(1.3f, 1.3f, 1.3f); // spin child node scale (tunable)

    // --- frequency rings (concentric circles by actual radius) ---
    float ring_start_radius = 0.0f; // radius where ring 0 begins
    int   ring_count        = 9;   // number of frequency rings
    float ring_width        = 0.1f;// world radial width of each ring
    float ring_gap          = 0.07f; // base gap unit (scaled by Fibonacci per ring)
    float catenary_k        = 2.2f; // inverted-catenary curvature (ripple shape)

    // --- terrain / audio ---
    float tilt_deg      = 46.f;   // terrain tilt toward camera (0..30)
    float max_amp       = 0.9f;
    float comp_k        = 16.f;   // log1p compression strength
    float height_gain   = 0.4f;   // amplitude → ripple height

    // --- drift (per-point looping wobble, direct world units) ---
    float orbit_x_min = 0.012f, orbit_x_max = 0.054f; // x drift amplitude range
    float orbit_y_min = 0.012f, orbit_y_max = 0.054f; // z-plane drift amplitude range
    float orbit_z_min = 0.040f, orbit_z_max = 0.060f; // vertical drift amplitude range
    float drift_speed_min = 0.20f, drift_speed_max = 0.75f; // per-point speed range
    float drift_speed_mul = 1.0f; // global drift speed multiplier

    float bright_gain   = 2.2f;
    float base_size     = 5.0f;
    float size_gain     = 26.f;

    int   BLOOM_FRAMES  = 45;

    // -----------------------------------------------------------------------
    View5() = default;

    void create()
    {
        generate_points();
        assign_freq_rings();
        pt_verts.assign(pts.size(), {});
        rebuild_points((int)pts.size());

        // View5 node: TILT transform only (no render / no mesh here).
        auto tilt_tr = std::make_shared<Transform>();
        add_comp(tilt_tr);
        tilt_transform_ = tilt_tr.get();

        // Child node: SPIN transform + Render + points. Model = tilt · spin.
        spin_node_ = std::make_shared<Node<Component>>();
        auto spin_tr = std::make_shared<Transform>();
        spin_node_->add_comp(spin_tr);
        spin_transform_ = spin_tr.get();

        auto render = std::shared_ptr<Render>(
            new Render("base/psize_vs.glsl", "base/glow_fg.glsl"));
        render->init();
        auto prog = render->get();
        if (prog->uniform_id("perspective") == -1)
            prog->locat_uniforms("perspective", "world", "model", "uIsPoint");
        spin_node_->add_comp<Render>(render);

        bi = std::make_shared<BiMeshComp>();
        bi->uIsPoint_loc = prog->uniform_id("uIsPoint");
        create_vaos();
        spin_node_->add_comp<BiMeshComp>(bi);

        add_child(spin_node_);
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

    // -----------------------------------------------------------------------
    // Colour every point from the album cover (or random noise if none).
    // -----------------------------------------------------------------------
    void set_cover(const fv::CoverArt* cover)
    {
        bool has = (cover && cover->valid());
        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> u01(0.f, 1.f);

        for (auto& p : pts) {
            if (has) {
                // Normalise by the actual cluster half-extent so the full cover
                // maps across the point cluster.
                float u = p.anchor.x / ext_x_ * 0.5f + 0.5f;
                float v = p.anchor.y / ext_y_ * 0.5f + 0.5f;
                p.base_col = cover->get_color(u, 1.0f - v);
            } else {
                p.base_col = glm::vec3(u01(rng), u01(rng), u01(rng)); // noise
            }
        }
    }

    // -----------------------------------------------------------------------
    void on_update(float* data, int len) override
    {
        int N = (int)pts.size();

        if (bloom_frame < BLOOM_FRAMES) {
            float t = (float)bloom_frame / (float)BLOOM_FRAMES;
            bloom_t = 1.f - std::pow(1.f - t, 3.f);
            ++bloom_frame;
        } else {
            bloom_t = 1.f;
        }
        time_ += 0.016f;

        // Drive the two split transforms:
        //   View5 own transform → tilt (rotate.x); child node → Y spin + scale.
        if (tilt_transform_) tilt_transform_->rotate.x = glm::radians(tilt_deg);
        if (spin_transform_) {
            spin_transform_->rotate.y += spin_speed * 0.016f;
            if (spin_transform_->rotate.y > glm::two_pi<float>())
                spin_transform_->rotate.y -= glm::two_pi<float>();
            spin_transform_->scale = spin_scale;
        }

        int rc = (ring_count > 1) ? ring_count : 1;
        float cosh_k = std::cosh(catenary_k);
        float cat_den = (cosh_k - 1.f > 1e-5f) ? (cosh_k - 1.f) : 1e-5f;

        for (int i = 0; i < N; ++i) {
            Pt& p = pts[i];

            // ---- looping drift (per-axis amplitude, global speed mul) -------
            float sp = p.drift_speed * drift_speed_mul;
            float a1 = time_ * sp + p.phase;
            float a2 = time_ * sp * 1.3f + p.phase2;
            float a3 = time_ * sp * 0.7f + p.phase3;
            float du = std::cos(a1) * p.orbit_x;
            float dv = std::sin(a2) * p.orbit_y;
            float dh = std::sin(a3) * p.orbit_z;

            float H = p.baseH + dh;               // random base height + drift
            float amp_norm = 0.f;

            if (p.in_ring) {
                // frequency bin: CENTER = HIGH, OUTER = LOW
                int bin = (len > 0)
                    ? (int)std::lround((float)(rc - 1 - p.freq_ring)
                                       / (float)(rc - 1 > 0 ? rc - 1 : 1)
                                       * (float)(len - 1))
                    : 0;
                if (bin < 0) bin = 0; else if (bin > len - 1) bin = len - 1;

                float raw = std::log1p(std::sqrt(std::max(0.f, data[bin])) * comp_k)
                          / std::log1p(comp_k) * max_amp;
                p.amp = (raw > p.amp) ? raw : p.amp * 0.80f + raw * 0.20f;

                // inverted catenary: dist=0 → 1 (highest), dist=1 → 0 (lowest)
                float f = (cosh_k - std::cosh(catenary_k * p.dist)) / cat_den;

                H += f * p.amp * height_gain;
                amp_norm = p.amp / max_amp;
            }

            // Local ground plane = XZ (u→x, v→z); ripple height → local Y (up).
            // Tilt/spin are applied by the parent/child transforms.
            p.pos = glm::vec3(p.anchor.x + du, H, p.anchor.y + dv) * bloom_t;

            p.bright = 0.30f + amp_norm * bright_gain;
            p.size   = base_size + amp_norm * size_gain;
        }

        rebuild_points(N);
        upload_vaos();
    }

    size_t fft_data_length() override { return BASS_DATA_FFT1024; }

    void onDraw() override {
        glEnable(GL_PROGRAM_POINT_SIZE);
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
        glm::vec2 anchor;     // cell centre + jitter (world, xy plane)
        float baseH;          // random base height (z jitter)
        float rr;             // radial distance from centre
        glm::vec3 pos;
        glm::vec3 base_col;   // cover colour (or noise)
        float dist;           // |edge distance| in its ring, [0,1]
        int   freq_ring;      // which frequency ring (→ bin)
        bool  in_ring;        // false → responds to no frequency (stays flat)
        float orbit_x, orbit_y, orbit_z;
        float phase, phase2, phase3;
        float drift_speed;
        float amp    = 0.f;
        float bright = 0.f;
        float size   = 5.f;
    };

    // -----------------------------------------------------------------------
    // A) POINT GENERATION — honeycomb sized only by point_count + point_spacing.
    // -----------------------------------------------------------------------
    void generate_points()
    {
        // rings needed so N = 1 + 3R(R+1) >= point_count
        int R = (int)std::ceil((-1.0 + std::sqrt(1.0 + 4.0 * (double)(point_count - 1) / 3.0)) / 2.0);
        if (R < 1) R = 1;

        static const int DQ[6] = { +1, +1,  0, -1, -1,  0 };
        static const int DR[6] = {  0, -1, -1,  0, +1, +1 };

        std::vector<glm::ivec2> ax;
        ax.push_back({ 0, 0 });
        for (int k = 1; k <= R; ++k) {
            int q = -k, r = k;
            for (int side = 0; side < 6; ++side) {
                for (int s = 0; s < k; ++s) {
                    ax.push_back({ q, r });
                    q += DQ[side]; r += DR[side];
                }
            }
        }
        int N = (int)ax.size();

        // pointy-top hex → plane, uniform spacing (adjacent centre dist = point_spacing)
        const float S3 = std::sqrt(3.f);
        const float s_factor = point_spacing / S3;

        // inscribed-circle radius (world) = point_spacing/2; jitter ellipsoid = ×scaleXYZ
        const float rin   = point_spacing * 0.5f;
        const float rin_x = rin * scalex;
        const float rin_y = rin * scaley;
        const float rin_z = rin * scalez;

        pts.resize(N);
        std::mt19937 rng(2024);
        std::uniform_real_distribution<float> u11(-1.f, 1.f);
        std::uniform_real_distribution<float> u01(0.f, 1.f);
        std::uniform_real_distribution<float> uphase(0.f, glm::two_pi<float>());
        std::uniform_real_distribution<float> ox(orbit_x_min, orbit_x_max);
        std::uniform_real_distribution<float> oy(orbit_y_min, orbit_y_max);
        std::uniform_real_distribution<float> oz(orbit_z_min, orbit_z_max);
        std::uniform_real_distribution<float> uspeed(drift_speed_min, drift_speed_max);

        float ext_x = 1e-6f, ext_y = 1e-6f;

        for (int i = 0; i < N; ++i) {
            Pt& p = pts[i];
            float q = (float)ax[i].x, r = (float)ax[i].y;
            float cx = (S3 * q + S3 * 0.5f * r) * s_factor;
            float cy = (1.5f * r) * s_factor;

            // random point inside the inscribed-circle ellipsoid → final position
            glm::vec3 off;
            do { off = glm::vec3(u11(rng), u11(rng), u11(rng)); }
            while (glm::dot(off, off) > 1.f);

            p.anchor = glm::vec2(cx + off.x * rin_x, cy + off.y * rin_y);
            p.baseH  = off.z * rin_z;
            p.rr     = std::sqrt(p.anchor.x * p.anchor.x + p.anchor.y * p.anchor.y);

            ext_x = std::max(ext_x, std::fabs(p.anchor.x));
            ext_y = std::max(ext_y, std::fabs(p.anchor.y));

            p.base_col = glm::vec3(u01(rng), u01(rng), u01(rng)); // default noise
            p.orbit_x = ox(rng);
            p.orbit_y = oy(rng);
            p.orbit_z = oz(rng);
            p.phase   = uphase(rng);
            p.phase2  = uphase(rng);
            p.phase3  = uphase(rng);
            p.drift_speed = uspeed(rng);
            p.pos     = glm::vec3(0.f);
        }

        ext_x_ = ext_x;
        ext_y_ = ext_y;
    }

    // -----------------------------------------------------------------------
    // B) FREQUENCY RINGS — concentric circles by actual radius; per point store
    //    which ring it falls in and its normalised distance to the ring edge.
    // -----------------------------------------------------------------------
    // Gap (space) between frequency ring `index` and the next one.
    // Fibonacci-scaled: gap(i) = fib(i) * ring_gap  with fib = 1,1,2,3,5,8,...
    //   i=0→0.1, i=1→0.1, i=2→0.2, i=3→0.3, i=4→0.5, ...  (ring_gap = 0.1)
    float ring_space(int index) const
    {
        return ring_gap;
        int a = 1, b = 1;                 // fib(0), fib(1)
        for (int i = 0; i < index; ++i) { int t = a + b; a = b; b = t; }
        return (float)a * ring_gap;       // a == fib(index)
    }

    // -----------------------------------------------------------------------
    void assign_freq_rings()
    {
        float rw = (ring_width > 1e-5f) ? ring_width : 1e-5f;
        for (auto& p : pts) {
            p.in_ring   = false;
            p.freq_ring = 0;
            p.dist      = 1.f;

            // walk the concentric rings outward, inserting a gap between each
            float inner = ring_start_radius;
            for (int idx = 0; idx < ring_count; ++idx) {
                float outer = inner + rw;
                if (p.rr >= inner && p.rr < outer) {
                    p.in_ring   = true;
                    p.freq_ring = idx;
                    float local = (p.rr - inner) / rw;      // [0,1]
                    float s     = local * 2.f - 1.f;         // [-1,1] to ring edge
                    p.dist      = std::fabs(s);              // [0,1], 0 = ring middle
                    break;
                }
                // points landing in the gap keep in_ring=false (stay flat)
                inner = outer + ring_space(idx);
            }
        }
    }

    // -----------------------------------------------------------------------
    void rebuild_points(int n)
    {
        for (int i = 0; i < n; ++i) {
            const Pt& p = pts[i];
            pt_verts[i] = { p.pos, p.base_col * p.bright, p.size };
        }
    }

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

    void create_vaos()
    {
        bi->pt_vao = std::make_shared<gld::VertexArr>();
        bi->pt_vao->create();
        bi->pt_vao->create_arr<gld::ArrayBufferType::VERTEX>();
        upload_pts(bi->pt_vao);
        bi->pt_count = (int)pt_verts.size();
    }

    void upload_vaos()
    {
        upload_pts(bi->pt_vao);
        bi->pt_count = (int)pt_verts.size();
    }

    // -----------------------------------------------------------------------
    float bloom_t     = 0.f;
    int   bloom_frame = 0;
    float time_       = 0.f;
    float ext_x_ = 1.f, ext_y_ = 1.f;

    std::vector<Pt>          pts;
    std::vector<PointVertex> pt_verts;
    std::shared_ptr<BiMeshComp> bi;

    std::shared_ptr<Node<Component>> spin_node_;      // child: spin + points
    Transform* tilt_transform_ = nullptr;             // View5 own: tilt
    Transform* spin_transform_ = nullptr;             // child: Y spin + scale
};

} // namespace gld
