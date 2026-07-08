#define _CRT_SECURE_NO_WARNINGS
// text3d – demo: a 2D text layer (a formally-typeset classical Chinese poem
// tiled across the window) with a 3D text layer on top (a few randomly chosen
// characters lifted out with a perspective projection + z offset, spinning
// about their x / y axes over time).
//
// Key idea – both layers use the SAME glyph shader (base/word_vs_2), whose
// vertex stage is  gl_Position = ortho * perspective * world * model * v.
// Coordinates are window PIXELS (origin centred, +y up); `ortho` maps them to
// clip space and is shared by both layers, so text is positioned in pixels.
//   * 2D layer : perspective = I            -> ortho * model      (flat poem)
//   * 3D layer : perspective = z-fix matrix -> ortho * P * model  (foreshorten)
// The z-fixing perspective P leaves the z = 0 plane untouched (w = 1 - z/D), so
// a lifted character (z > 0) rises straight out of its exact flat position and
// is magnified toward the viewer, while unlifted points stay put.

#include <resource_mgr.hpp>
#include <FindPath.hpp>
#include <glad/glad.h>
#include <RenderDemoRotate.hpp>
#include <cstdio>
#include <memory>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>
#include <data_mgr.hpp>
#include <component.h>
#include <uniform.hpp>
#include <glm_uniform.hpp>
#include <text/TextMgr.hpp>
#include <text/TextNodeGen.hpp>
#include <ui/word.h>

using namespace gld;
namespace fs = std::filesystem;
using namespace txt;

class Demo : public RenderDemoRotate {
public:
    Demo() : uOrtho("ortho"), uPersp("perspective"), uWorld("world") {}

    int init() override
    {
        RenderDemoRotate::init();

        glDisable(GL_DEPTH_TEST);              // layers ordered by draw order
        glEnable(GL_FRAMEBUFFER_SRGB);
        glClearColor(0.055f, 0.05f, 0.075f, 1.f);

        // Shared glyph program (all Words render through this).
        word_prog = DefDataMgr::instance()->load<DataType::Program>(
            "base/word_vs_2.glsl", "base/word_fg.glsl");

        word_prog->locat_uniforms("ortho", "perspective", "world", "model", "diffuseTex", "textColor");


        build_poem();

        // Parent node that carries all floaters; mouse drag rotates this node and
        // Transform::get_model() composes it into every child glyph automatically.
        float_root_ = std::make_shared<Node<Component>>();
        float_root_->add_comp<Transform>(std::make_shared<Transform>());

        pick_floaters();
        update_viewport();
        return 0;
    }

    // Query the real framebuffer size (robust to DPI) and set viewport + matrices.
    void update_viewport()
    {
        int fbw = width, fbh = height;
        if (auto win = get_window())
            glfwGetFramebufferSize(win, &fbw, &fbh);
        if (fbw <= 0 || fbh <= 0) { fbw = width; fbh = height; }
        fb_w_ = fbw; fb_h_ = fbh;
        glViewport(0, 0, fbw, fbh);
        update_matrix();
        std::printf("[text3d] win=%dx%d fb=%dx%d aspect=%.3f\n",
                    width, height, fbw, fbh, (float)fbw / (float)fbh);
        std::fflush(stdout);
    }

    // Route this glyph's node through the ortho+perspective shader (word_vs_2).
    // Must be called after load() and before init() so the node's Transform /
    // material attach their uniforms to the SAME shared program the demo drives.
    static void use_ortho_shader(const std::shared_ptr<Word>& w)
    {
        if (auto* r = w->get_comp<Render>())
            r->vert_path = "base/word_vs_2.glsl";
    }

    // -----------------------------------------------------------------------
    // A single laid-out poem glyph (its flat position + info to clone it in 3D).
    struct Glyph {
        std::shared_ptr<Word> w;
        glm::vec3 base_pos;   // centre position in world space (z = 0)
        uint32_t  cp;         // codepoint
        int       size;
        bool      body;       // part of the poem body (candidate to float)
        bool      floating = false;  // lifted into the 3D layer -> skip in the flat 2D layer
    };
    struct Floater {
        std::shared_ptr<Word> w;
        float px = 0.f, py = 0.f;   // screen anchor in pixels (perspective is applied about this point)
        float ampx = 0.f, ampy = 0.f;   // tilt amplitude (radians) about x / y
        float wx = 0.f, wy = 0.f;       // angular frequency (rad/s)
        float phx = 0.f, phy = 0.f;     // phase offset (radians)
        float t = 0.f;                  // accumulated time (s)
    };

    // -----------------------------------------------------------------------
    // Lay out one centred row of glyphs at vertical centre y_center.
    void layout_row(std::string& font, int size, const std::u32string& text,
                    float y_center, glm::vec4 color, bool body)
    {
        struct Tmp { std::shared_ptr<Word> w; float adv; uint32_t cp; };
        std::vector<Tmp> tmp;
        float row_w = 0.f;
        for (char32_t c : text) {
            auto w = std::make_shared<Word>(font, size, (uint32_t)c, glm::vec2(0.5f, 0.5f));
            w->load();
            use_ortho_shader(w);
            float adv = (float)w->wd.advance * content_scale_;
            row_w += adv;
            tmp.push_back({ w, adv, (uint32_t)c });
        }

        float x = -row_w * 0.5f;
        float first_cx = 0, last_cx = 0;
        for (auto& t : tmp) {
            float cx = x + t.adv * 0.5f;
            if (&t == &tmp.front()) first_cx = cx;
            last_cx = cx;
            auto tr = t.w->get_comp<Transform>();
            tr->pos = glm::vec3(cx, y_center, 0.f);
            tr->scale = glm::vec3((float)t.w->wd.w * content_scale_,
                                  (float)t.w->wd.h * content_scale_, 1.f);
            t.w->get_comp<DefTextMaterial>()->color = color;
            t.w->init();
            poem2d_.push_back(Glyph{ t.w, glm::vec3(cx, y_center, 0.f), t.cp, size, body });
            x += t.adv;
        }
        std::printf("[text3d] row size=%d n=%zu row_w=%.4f y=%.4f first_cx=%.4f last_cx=%.4f\n",
                    size, tmp.size(), row_w, y_center, first_cx, last_cx);
        std::fflush(stdout);
    }

    // -----------------------------------------------------------------------
    void build_poem()
    {
        // 《静夜思》 — 李白
        struct Line { std::u32string text; int size; glm::vec4 color; bool body; };
        std::vector<Line> lines = {
            { U"静夜思",              210, glm::vec4(0.98f, 0.90f, 0.62f, 1.f), false },
            { U"〔唐〕李白",           92, glm::vec4(0.72f, 0.74f, 0.80f, 1.f), false },
            { U"床前明月光，疑是地上霜。", 128, glm::vec4(0.93f, 0.92f, 0.88f, 1.f), true  },
            { U"举头望明月，低头思故乡。", 128, glm::vec4(0.93f, 0.92f, 0.88f, 1.f), true  },
        };

        // section gaps (extra vertical spacing, in world units)
        float cs = content_scale_;
        auto line_h = [cs](int size) { return (float)size * cs * 1.55f; };
        float gap_title  = 25.f;    // after title (px)
        float gap_author = 45.f;    // after author, before body (px)

        // total height to vertically centre the block
        float total = 0.f;
        for (size_t i = 0; i < lines.size(); ++i) {
            total += line_h(lines[i].size);
            if (i == 0) total += gap_title;
            if (i == 1) total += gap_author;
        }

        float cursor = total * 0.5f;   // top of block
        for (size_t i = 0; i < lines.size(); ++i) {
            float lh = line_h(lines[i].size);
            float y_center = cursor - lh * 0.5f;
            layout_row(font_, lines[i].size, lines[i].text, y_center, lines[i].color, lines[i].body);
            cursor -= lh;
            if (i == 0) cursor -= gap_title;
            if (i == 1) cursor -= gap_author;
        }
    }

    // -----------------------------------------------------------------------
    static bool is_punct(uint32_t c)
    {
        return c == U'，' || c == U'。' || c == U'、' || c == U'；' ||
               c == U'！' || c == U'？' || c == U'：' || c == U'〔' || c == U'〕';
    }

    void pick_floaters()
    {
        std::mt19937 rng(std::random_device{}());

        // candidate indices: body glyphs, non-punctuation
        std::vector<int> cand;
        for (int i = 0; i < (int)poem2d_.size(); ++i)
            if (poem2d_[i].body && !is_punct(poem2d_[i].cp))
                cand.push_back(i);

        std::shuffle(cand.begin(), cand.end(), rng);
        int k = std::min((int)cand.size(), 7);

        std::uniform_real_distribution<float> uz(150.f, 560.f);   // lift toward camera (px)
        std::uniform_real_distribution<float> uamp(glm::radians(16.f), glm::radians(30.f)); // tilt amplitude
        std::uniform_real_distribution<float> uw(0.6f, 1.4f);     // angular frequency (rad/s)
        std::uniform_real_distribution<float> uphase(0.f, glm::two_pi<float>());
        auto sign = [&](std::mt19937& r) { return (r() & 1) ? 1.f : -1.f; };

        for (int i = 0; i < k; ++i) {
            int idx = cand[i];
            poem2d_[idx].floating = true;   // exclude its flat copy from the 2D layer
            const Glyph& g = poem2d_[idx];
            auto w = std::make_shared<Word>(font_, g.size, g.cp, glm::vec2(0.5f, 0.5f));
            w->load();
            use_ortho_shader(w);
            auto tr = w->get_comp<Transform>();
            tr->pos = glm::vec3(0.f, 0.f, uz(rng));   // only the lift; x,y come from the perspective uniform
            tr->scale = glm::vec3((float)w->wd.w * content_scale_,
                                  (float)w->wd.h * content_scale_, 1.f);
            w->get_comp<DefTextMaterial>()->color = glm::vec4(1.0f, 0.78f, 0.32f, 1.f); // warm gold
            w->init();

            Floater f;
            f.w  = w;
            f.px = g.base_pos.x;
            f.py = g.base_pos.y;
            f.ampx = sign(rng) * uamp(rng);   // sign varies the precession direction
            f.ampy = uamp(rng);
            f.wx = uw(rng);
            f.wy = f.wx;                      // same freq as x -> steady conical wobble
            f.phx = uphase(rng);
            f.phy = f.phx + glm::half_pi<float>();  // quadrature: y leads x by 90 deg
            float3d_.push_back(std::move(f));
            float_root_->add_child(std::static_pointer_cast<Node<Component>>(w)); // parented for drag rotation
        }
    }

    // -----------------------------------------------------------------------
    void sync_layer(const glm::mat4& persp, const glm::mat4& world)
    {
        word_prog->use();
        uOrtho = ortho_;  uOrtho.attach_program(word_prog); uOrtho.sync();
        uPersp = persp;  uPersp.attach_program(word_prog); uPersp.sync();
        uWorld = world;  uWorld.attach_program(word_prog); uWorld.sync();
    }

    void draw() override
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // --- 2D layer : screen-space ortho, perspective = identity (flat poem) ---
        sync_layer(glm::mat4(1.f), glm::mat4(1.f));
        for (auto& g : poem2d_)
            if (!g.floating)          // its lifted copy lives in the 3D layer instead
                g.w->draw();

        // --- 3D layer : per-glyph z-fixing perspective. Each floater is magnified
        //     about ITS OWN anchor (no lateral drift) and lifts toward the viewer. ---
        word_prog->use();
        uOrtho = ortho_;          uOrtho.attach_program(word_prog); uOrtho.sync();
        uWorld = glm::mat4(1.f);  uWorld.attach_program(word_prog); uWorld.sync();

        uPersp.attach_program(word_prog);
        for (auto& f : float3d_) {
            word_prog->use();     // rebind: the previous glyph's after_draw unbound the program
            uPersp = glm::translate(glm::mat4(1.f), glm::vec3(f.px, f.py, 0.f)) * persp_;
            uPersp.sync();
            f.w->draw();
        }

        update();
    }

    void update()
    {
        const float dt = 0.016f;
        for (auto& f : float3d_) {
            f.t += dt;
            auto t = f.w->get_comp<Transform>();
            // Gentle looping tilt (~+/-15 deg) about x / y — never a full flip.
            t->rotate.x = f.ampx * std::sin(f.wx * f.t + f.phx);   // radians
            t->rotate.y = f.ampy * std::sin(f.wy * f.t + f.phy);
        }

        // When not dragging, slowly spring the parent rotation back to 0.
        if (!mouse_key_left_press) {
            float decay = 1.f - std::pow(spring_back_, dt);  // frame-rate independent
            drag_rot_ -= drag_rot_ * decay;
            if (glm::length(drag_rot_) < 1e-4f) drag_rot_ = glm::vec2(0.f);
        }

        // Apply the accumulated drag rotation to the floater parent node.
        auto rt = float_root_->get_comp<Transform>();
        rt->rotate.x = drag_rot_.x;   // pitch (radians)
        rt->rotate.y = drag_rot_.y;   // yaw
    }

    void update_matrix()
    {
        // Shared screen-space ortho: coordinates are window pixels, origin at the
        // centre, +y up. Wide z range so lifted / spinning glyphs never clip.
        float W = (float)fb_w_, H = (float)fb_h_;
        ortho_ = glm::ortho(-W * 0.5f, W * 0.5f, -H * 0.5f, H * 0.5f, -4000.f, 4000.f);

        // z-fixing perspective for the 3D layer. It leaves the z = 0 plane exactly
        // where the flat 2D layout put it, and foreshortens lifted glyphs:
        //   w = 1 - z / D  ->  x,y are divided by w, magnifying points with z > 0.
        persp_ = glm::mat4(1.f);
        persp_[2][3] = -1.f / D_;
    }

    void onWindowResize(int w, int h) override
    {
        RenderDemoRotate::onWindowResize(w, h);
        update_viewport();
    }

    // Left-drag rotates the floater parent node (pitch from vertical, yaw from
    // horizontal), clamped to [rot_min_, rot_max_]. Released rotation springs back.
    void onMouseMove(double x, double y) override
    {
        if (mouse_key_left_press)
        {
            if (mouse_key_left_press_first)
            {
                mouse_key_left_press_first = false;
                last_mouse_pos = glm::vec2((float)x, (float)y);
            }
            float dx = (float)x - last_mouse_pos.x;
            float dy = (float)y - last_mouse_pos.y;
            drag_rot_.y += rot_speed_ * dx;   // horizontal drag -> yaw (y axis)
            drag_rot_.x += rot_speed_ * dy;   // vertical drag   -> pitch (x axis)
            drag_rot_.x = glm::clamp(drag_rot_.x, rot_min_.x, rot_max_.x);
            drag_rot_.y = glm::clamp(drag_rot_.y, rot_min_.y, rot_max_.y);
        }
        last_mouse_pos = glm::vec2((float)x, (float)y);
    }

private:
    std::shared_ptr<Program> word_prog;
	GlmUniform<UT::Matrix4> uOrtho;
    GlmUniform<UT::Matrix4> uPersp, uWorld;
    glm::mat4 persp_ = glm::mat4(1.f), ortho_ = glm::mat4(1.f);

    std::vector<Glyph>   poem2d_;
    std::vector<Floater> float3d_;

    // Parent node holding all floaters; its rotation is driven by mouse drag.
    std::shared_ptr<Node<Component>> float_root_;
    glm::vec2 drag_rot_ = glm::vec2(0.f);                        // current drag rotation (x=pitch, y=yaw), radians
    glm::vec2 rot_min_  = glm::vec2(-glm::radians(15.f));        // lower limit (configurable)
    glm::vec2 rot_max_  = glm::vec2( glm::radians(15.f));        // upper limit (configurable)
    float     rot_speed_   = 0.001f;                             // radians per pixel of drag
    float     spring_back_ = 0.02f;                            // per-second retention (smaller = snappier)

    std::string font_ = "fonts/simkai.ttf";
    float content_scale_ = 0.6f;  // atlas px -> screen px (keeps the poem inside the window)
    float D_ = 900.f;             // virtual camera distance (px) driving 3D foreshortening
    int   fb_w_ = 1, fb_h_ = 1;   // framebuffer size (pixels)
};

#ifndef PF_ANDROID
int main()
{
    fs::path root = wws::find_path(3, "res", true);
    ResMgrWithGlslPreProcess::create_instance(root);
    DefResMgr::create_instance(std::move(root));
    Demo d;
    if (d.initWindow(1280, 800, "text3d - 2D poem + 3D floating glyphs"))
    {
        printf("init window failed\n");
        return -1;
    }
    d.init();
    d.run();
    return 0;
}
#endif
