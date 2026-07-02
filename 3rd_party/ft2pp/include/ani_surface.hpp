#pragma once

/*
 * ani_surface.hpp – animated pixel-particle surface system (wws namespace)
 *
 * Exposes:
 *   wws::vec2f           – 2-D float vector (used as particle coordinates)
 *   wws::point           – single particle: pos, tar, v, distance
 *   wws::ASDrive<C>      – abstract drive: feeds text & controls transitions
 *   wws::AniSurface<C>   – manages two surfaces + particle animation between them
 *
 * Typical usage (clock demo):
 *   auto sur = std::make_shared<AniSurface<GLContent>>(pw, ph, drive.get(), color, 300);
 *   sur->to_out_speed = 0.09f;
 *   sur->to_use_speed = 0.10f;
 *   sur->move_to_func  = [](wws::point& p){ ... };
 *   sur->set_min_frame_ms(16);
 *   sur->pre_go();
 *   // per frame:
 *   if (!sur->is_end()) sur->ani_step();
 */

#include <surface.hpp>
#include <functional>
#include <vector>
#include <cmath>
#include <chrono>
#include <random>
#include <cstdint>

namespace wws {

// ---------------------------------------------------------------------------
// vec2f – minimal 2-D float vector
// ---------------------------------------------------------------------------
struct vec2f {
    float x_ = 0.f, y_ = 0.f;

    vec2f() = default;
    vec2f(float x, float y) : x_(x), y_(y) {}

    float x() const noexcept { return x_; }
    float y() const noexcept { return y_; }

    float len() const noexcept { return std::sqrt(x_ * x_ + y_ * y_); }

    vec2f operator+(const vec2f& o) const noexcept { return { x_ + o.x_, y_ + o.y_ }; }
    vec2f operator-(const vec2f& o) const noexcept { return { x_ - o.x_, y_ - o.y_ }; }
    vec2f operator*(float f)        const noexcept { return { x_ * f,    y_ * f    }; }
    vec2f& operator+=(const vec2f& o) noexcept { x_ += o.x_; y_ += o.y_; return *this; }
    bool operator==(const vec2f& o) const noexcept { return x_ == o.x_ && y_ == o.y_; }
};

// ---------------------------------------------------------------------------
// point – one particle
// ---------------------------------------------------------------------------
struct point {
    vec2f pos;       // current position in pixel-space
    vec2f tar;       // target (home) position
    vec2f v;         // velocity direction (unit vector toward tar)
    float distance = 0.f; // original distance from scatter pos to target
    bool  active   = false;
    bool  lit      = false; // belongs to the current text frame (survives active→false)
};

// ---------------------------------------------------------------------------
// ASDrive<Content> – abstract animation driver
// ---------------------------------------------------------------------------
template<typename Content>
struct ASDrive {
    using PixelTy = typename Content::PIXEL_TYPE;

    // Write the current text/frame into `sur` using color `f_c`.
    virtual void set_text(surface<Content>& sur, PixelTy f_c) = 0;

    // Advance internal state (called once per successful transfer).
    virtual void step() = 0;

    // Return the callback that receives the pixel data after each present.
    virtual std::function<void(const Content*)> get_present() = 0;

    // Return true when a surface transfer should occur.
    //   ms           – elapsed ms since the last transfer
    //   to_use_stable – incoming animation finished
    //   to_out_stable – outgoing animation finished
    virtual bool need_transfar(uint32_t ms, bool to_use_stable, bool to_out_stable) = 0;

    // Return true when the whole display should stop (e.g. one-shot animation).
    virtual bool is_end() = 0;

    virtual ~ASDrive() = default;
};

// ---------------------------------------------------------------------------
// AniSurface<Content>
// ---------------------------------------------------------------------------
template<typename Content>
class AniSurface {
public:
    using PixelTy = typename Content::PIXEL_TYPE;

    // Public animation parameters (set before pre_go())
    float to_out_speed = 0.05f;   // scatter speed multiplier
    float to_use_speed = 0.05f;   // reassemble speed multiplier

    // Optional per-particle movement override. If set, called instead of the
    // default exponential-approach for the TO_USE phase.
    std::function<void(point&)> move_to_func;

    // -----------------------------------------------------------------------
    // Constructor
    //   w, h        – surface dimensions in pixels
    //   drive       – animation driver (not owned)
    //   color       – foreground pixel value (e.g. glm::vec3 for GLContent)
    //   scatter_dist – maximum scatter distance in pixels (e.g. 300)
    // -----------------------------------------------------------------------
    AniSurface(int w, int h, ASDrive<Content>* drive,
               PixelTy color, uint32_t scatter_dist)
        : w_(w), h_(h), drive_(drive), color_(color),
          scatter_dist_(static_cast<float>(scatter_dist)),
          use_sur(w, h), out_sur(w, h)
    {
        points_.resize(static_cast<size_t>(w) * static_cast<size_t>(h));
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                auto& p = points_[y * w + x];
                p.tar = vec2f(static_cast<float>(x), static_cast<float>(y));
                p.pos = p.tar;
                p.active = false;
            }
    }

    void set_min_frame_ms(uint32_t ms) { min_frame_ms_ = ms; }

    bool is_end() { return drive_->is_end(); }

    // Call once after construction to populate the first frame.
    void pre_go() {
        present_cb_ = drive_->get_present();
        use_sur.get_content().clear();
        drive_->set_text(use_sur, color_);
        if (present_cb_) use_sur.get_content().present(present_cb_);

        last_transfer_tp_ = clock_t::now();
        state_ = State::USING;
    }

    // Call every frame (from the render loop).
    void ani_step() {
        auto now = clock_t::now();
        uint32_t frame_ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_frame_tp_).count());
        last_frame_tp_ = now;

        if (frame_ms < min_frame_ms_) return;

        uint32_t elapsed = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_transfer_tp_).count());

        switch (state_) {

        case State::USING: {
            bool to_use_stable = true;
            bool to_out_stable = true;
            if (drive_->need_transfar(elapsed, to_use_stable, to_out_stable)) {
                present_cb_ = drive_->get_present();
                drive_->step();
                out_sur.get_content().clear();
                drive_->set_text(out_sur, color_);
                setup_to_out_particles();
                state_ = State::TO_OUT;
                last_transfer_tp_ = now;
            }
            break;
        }

        case State::TO_OUT: {
            bool stable = step_to_out();
            render_out_frame();          // draw particles, call present every frame
            if (stable) {
                setup_to_use_particles();
                state_ = State::TO_USE;
            }
            break;
        }

        case State::TO_USE: {
            bool stable = step_to_use();
            render_use_frame();          // draw particles, call present every frame
            if (stable) {
                state_ = State::USING;
                last_transfer_tp_ = now;
            }
            break;
        }
        }
    }

private:
    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------
    static vec2f random_scatter_pos(float cx, float cy, float dist,
                                     std::mt19937& rng) {
        std::uniform_real_distribution<float> angle_d(0.f, 6.2831853f);
        std::uniform_real_distribution<float> mag_d(dist * 0.5f, dist);
        float a = angle_d(rng);
        float m = mag_d(rng);
        return { cx + std::cos(a) * m, cy + std::sin(a) * m };
    }

    // Scatter active (lit) pixels of use_sur to random positions.
    void setup_to_out_particles() {
        std::mt19937 rng(std::random_device{}());
        float cx = w_ * 0.5f, cy = h_ * 0.5f;
        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                auto& p = points_[y * w_ + x];
                auto px = use_sur.get_pixel(x, y);
                bool lit = !(px == Content::EMPTY_PIXEL);
                p.lit = lit;
                if (lit) {
                    p.pos = vec2f(static_cast<float>(x), static_cast<float>(y));
                    p.tar = random_scatter_pos(cx, cy, scatter_dist_, rng);
                    auto d = p.tar - p.pos;
                    float dl = d.len();
                    p.v = dl > 0.f ? d * (1.f / dl) : vec2f(1.f, 0.f);
                    p.distance = dl;
                    p.active = true;
                } else {
                    p.active = false;
                }
            }
        }
    }

    // Set up particles so lit pixels of out_sur fly in from random positions.
    void setup_to_use_particles() {
        std::mt19937 rng(std::random_device{}());
        float cx = w_ * 0.5f, cy = h_ * 0.5f;
        // Copy out_sur content into use_sur so callers can read it
        use_sur.get_content().swap(out_sur.get_content());

        for (int y = 0; y < h_; ++y) {
            for (int x = 0; x < w_; ++x) {
                auto& p = points_[y * w_ + x];
                auto px = use_sur.get_pixel(x, y);
                bool lit = !(px == Content::EMPTY_PIXEL);
                p.lit = lit;
                if (lit) {
                    p.tar = vec2f(static_cast<float>(x), static_cast<float>(y));
                    p.pos = random_scatter_pos(cx, cy, scatter_dist_, rng);
                    auto d = p.tar - p.pos;
                    float dl = d.len();
                    p.v = dl > 0.f ? d * (1.f / dl) : vec2f(0.f, 0.f);
                    p.distance = dl;
                    p.active = true;
                } else {
                    p.active = false;
                }
            }
        }
    }

    // Move particles outward; return true when all are stable.
    bool step_to_out() {
        constexpr float THRESH = 1.5f;
        bool all_done = true;
        for (auto& p : points_) {
            if (!p.active) continue;
            float rem = (p.tar - p.pos).len();
            if (rem < THRESH) { p.active = false; continue; }
            all_done = false;
            float step = rem * to_out_speed;
            p.pos = p.pos + (p.v * step);
        }
        return all_done;
    }

    // Move particles toward their home pixel; return true when all are stable.
    bool step_to_use() {
        constexpr float THRESH = 0.8f;
        bool all_done = true;
        for (auto& p : points_) {
            if (!p.active) continue;
            float rem = (p.tar - p.pos).len();
            if (rem < THRESH) {
                p.pos = p.tar;
                p.active = false;
                continue;
            }
            all_done = false;
            if (move_to_func) {
                move_to_func(p);
            } else {
                float step = rem * to_use_speed;
                p.pos = p.pos + (p.v * step); // v points toward tar
            }
        }
        return all_done;
    }

    // Render TO_OUT intermediate frame: active lit particles at current pos.
    void render_out_frame() {
        use_sur.get_content().clear();
        for (auto& p : points_) {
            if (!p.active || !p.lit) continue;
            int px = static_cast<int>(p.pos.x_ + 0.5f);
            int py = static_cast<int>(p.pos.y_ + 0.5f);
            if (px >= 0 && px < w_ && py >= 0 && py < h_)
                use_sur.set_pixel(px, py, color_);
        }
        if (present_cb_) use_sur.get_content().present(present_cb_);
    }

    // Render TO_USE intermediate frame: active particles at current pos,
    // already-arrived particles at their target.
    void render_use_frame() {
        use_sur.get_content().clear();
        for (auto& p : points_) {
            if (!p.lit) continue;
            vec2f draw_pos = p.active ? p.pos : p.tar;
            int px = static_cast<int>(draw_pos.x_ + 0.5f);
            int py = static_cast<int>(draw_pos.y_ + 0.5f);
            if (px >= 0 && px < w_ && py >= 0 && py < h_)
                use_sur.set_pixel(px, py, color_);
        }
        if (present_cb_) use_sur.get_content().present(present_cb_);
    }

    // -----------------------------------------------------------------------
    // Data
    // -----------------------------------------------------------------------
    int w_, h_;
    ASDrive<Content>* drive_;   // non-owning
    PixelTy color_;
    float scatter_dist_;

    surface<Content> use_sur;  // currently shown surface
    surface<Content> out_sur;  // surface being prepared

    std::vector<point> points_;

    std::function<void(const Content*)> present_cb_;

    uint32_t min_frame_ms_ = 16;

    enum class State { IDLE, USING, TO_OUT, TO_USE } state_ = State::IDLE;

    using clock_t = std::chrono::steady_clock;
    clock_t::time_point last_transfer_tp_ = clock_t::now();
    clock_t::time_point last_frame_tp_    = clock_t::now();
};

} // namespace wws
