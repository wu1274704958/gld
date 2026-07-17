// aoe2de_spearman - displays locally converted AoE2DE Spearman SLD graphics.
#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include <glm\glm.hpp>
#include <glm\gtc\matrix_transform.hpp>

#include <FindPath.hpp>
#include <program.hpp>
#include <resource_mgr.hpp>
#include <texture.hpp>

#include <ecs\App.hpp>
#include <ecs\Components.hpp>
#include <ecs\Input.hpp>
#include <ecs\Window.hpp>
#include <ecs\assets\AssetServer.hpp>
#include <ecs\assets\FileSystem.hpp>
#include <ecs\render\Batch.hpp>
#include <ecs\render\BatchSystem.hpp>
#include <ecs\render\RenderComponents.hpp>
#include <ecs\render\RenderSystem.hpp>
#include <ecs\systems\TransformSystem.hpp>

using namespace gld::ecs;
namespace fs = std::filesystem;

namespace {
    constexpr std::uint32_t kLayer = 0x1u;

    struct FrameDef {
        glm::vec4 uv{ 0.f };
        glm::vec2 size{ 0.f };
        glm::vec2 hotspot{ 0.f };
    };

    struct AnimDef {
        std::string name;
        std::string texture_path;
        float fps = 12.f;
        int display_direction_count = 8;
        std::vector<std::vector<FrameDef>> directions;
        Handle<gld::Texture<gld::TexType::D2>> texture;
    };

    struct DemoState {
        entt::entity camera = entt::null;
        entt::entity batch = entt::null;
        Handle<gld::Program> shader;
        std::vector<AnimDef> animations;
        int current = 0;
        float time = 0.f;
        float scale = 1.35f;
    };

    std::string read_text(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return {};
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    fs::path find_res_root() {
        fs::path cur = fs::absolute(fs::current_path());
        for (int i = 0; i < 6; ++i) {
            fs::path candidate = cur / "res";
            if (fs::is_directory(candidate))
                return candidate;
            if (!cur.has_parent_path() || cur == cur.parent_path())
                break;
            cur = cur.parent_path();
        }
        return {};
    }

    std::string json_string(const std::string& text, const std::string& key) {
        std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
        std::smatch m;
        return std::regex_search(text, m, re) ? m[1].str() : std::string{};
    }

    int json_int(const std::string& text, const std::string& key, int fallback = 0) {
        std::regex re("\"" + key + "\"\\s*:\\s*(-?\\d+)");
        std::smatch m;
        return std::regex_search(text, m, re) ? std::stoi(m[1].str()) : fallback;
    }

    float json_float(const std::string& text, const std::string& key, float fallback = 0.f) {
        std::regex re("\"" + key + "\"\\s*:\\s*(-?\\d+(?:\\.\\d+)?)");
        std::smatch m;
        return std::regex_search(text, m, re) ? std::stof(m[1].str()) : fallback;
    }

    bool parse_animation(const fs::path& json_path, const std::string& name, AnimDef& out) {
        const std::string text = read_text(json_path);
        if (text.empty()) return false;

        const int atlas_w = json_int(text, "atlas_w", 1);
        const int atlas_h = json_int(text, "atlas_h", 1);
        const int direction_count = std::max(1, json_int(text, "direction_count", 8));
        out.name = name;
        out.texture_path = "aoe2de_cache/spearman/graphics/" + json_string(text, "image");
        out.fps = json_float(text, "fps", 12.f);
        out.display_direction_count = std::min(8, direction_count);
        out.directions.assign(static_cast<std::size_t>(direction_count), {});

        std::regex frame_re(
            "\\{\\s*\"x\"\\s*:\\s*(\\d+),\\s*\"y\"\\s*:\\s*(\\d+),\\s*\"w\"\\s*:\\s*(\\d+),\\s*\"h\"\\s*:\\s*(\\d+),\\s*"
            "\"hotspot_x\"\\s*:\\s*(-?\\d+),\\s*\"hotspot_y\"\\s*:\\s*(-?\\d+),\\s*"
            "\"direction\"\\s*:\\s*(\\d+),\\s*\"frame\"\\s*:\\s*(\\d+)\\s*\\}");

        for (std::sregex_iterator it(text.begin(), text.end(), frame_re), end; it != end; ++it) {
            const auto& m = *it;
            const int x = std::stoi(m[1].str());
            const int y = std::stoi(m[2].str());
            const int w = std::stoi(m[3].str());
            const int h = std::stoi(m[4].str());
            const int hx = std::stoi(m[5].str());
            const int hy = std::stoi(m[6].str());
            const int dir = std::stoi(m[7].str());
            const int frame = std::stoi(m[8].str());
            if (dir < 0 || dir >= direction_count) continue;

            auto& frames = out.directions[static_cast<std::size_t>(dir)];
            if (frames.size() <= static_cast<std::size_t>(frame))
                frames.resize(static_cast<std::size_t>(frame + 1));

            FrameDef def;
            def.uv = glm::vec4(
                static_cast<float>(x) / static_cast<float>(atlas_w),
                static_cast<float>(y) / static_cast<float>(atlas_h),
                static_cast<float>(w) / static_cast<float>(atlas_w),
                static_cast<float>(h) / static_cast<float>(atlas_h));
            def.size = glm::vec2(static_cast<float>(w), static_cast<float>(h));
            def.hotspot = glm::vec2(static_cast<float>(hx), static_cast<float>(hy));
            frames[static_cast<std::size_t>(frame)] = def;
        }

        return true;
    }

    bool load_manifest(const fs::path& root, DemoState& state) {
        const fs::path manifest_path = root / "aoe2de_cache" / "spearman" / "manifest.json";
        const std::string manifest = read_text(manifest_path);
        if (manifest.empty()) return false;

        const std::vector<std::string> names{ "idleA", "walkA", "attackA", "deathA" };
        for (const auto& name : names) {
            const std::string rel = json_string(manifest, name);
            if (rel.empty()) continue;
            AnimDef anim;
            if (parse_animation(root / "aoe2de_cache" / "spearman" / rel, name, anim))
                state.animations.push_back(std::move(anim));
        }
        return !state.animations.empty();
    }

    glm::vec2 panel_anchor(int direction, const Window& win) {
        const int cols = 4;
        const int rows = 2;
        const float cell_w = static_cast<float>(win.width) / static_cast<float>(cols);
        const float cell_h = static_cast<float>(win.height) / static_cast<float>(rows);
        const int row = direction / cols;
        const int col = direction % cols;
        return glm::vec2(
            -win.width * 0.5f + cell_w * (static_cast<float>(col) + 0.5f),
             win.height * 0.5f - cell_h * (static_cast<float>(row) + 0.72f));
    }

    void push_frame(BatchComponent& batch, const FrameDef& frame, glm::vec2 anchor, float scale) {
        const glm::vec2 center = anchor + glm::vec2(
            (frame.size.x * 0.5f - frame.hotspot.x) * scale,
            (frame.hotspot.y - frame.size.y * 0.5f) * scale);

        InstanceData d;
        d.rect = frame.uv;
        d.pad = frame.uv;
        d.color = glm::vec4(1.f);
        d.transform = glm::translate(glm::mat4(1.f), glm::vec3(center, 0.f)) *
            glm::scale(glm::mat4(1.f), glm::vec3(frame.size * scale, 1.f));
        batch.instances.push_back(d);
    }

    void input_system(EcsWorld& w) {
        auto* state = w.try_resource<DemoState>();
        auto* kb = w.try_resource<Keyboard>();
        if (!state || !kb) return;

        auto select = [&](int idx) {
            if (idx >= 0 && idx < static_cast<int>(state->animations.size())) {
                state->current = idx;
                state->time = 0.f;
                std::printf("[aoe2de_spearman] animation: %s\n", state->animations[idx].name.c_str());
            }
        };

        if (kb->just_now_pressed(GLFW_KEY_1)) select(0);
        if (kb->just_now_pressed(GLFW_KEY_2)) select(1);
        if (kb->just_now_pressed(GLFW_KEY_3)) select(2);
        if (kb->just_now_pressed(GLFW_KEY_4)) select(3);
        if (kb->just_now_pressed(GLFW_KEY_SPACE)) state->time = 0.f;
    }

    void render_spearman_system(EcsWorld& w) {
        auto* state = w.try_resource<DemoState>();
        auto* win = w.try_resource<Window>();
        auto* time = w.try_resource<Time>();
        if (!state || !win || state->batch == entt::null || !w.reg().valid(state->batch)) return;

        state->time += time ? time->dt : 0.016f;
        auto& batch = w.reg().get<BatchComponent>(state->batch);
        batch.used = true;
        batch.instances.clear();

        if (state->animations.empty()) {
            batch.dirty = true;
            return;
        }

        auto& anim = state->animations[static_cast<std::size_t>(state->current)];
        gld::Program* prog = state->shader.get();
        auto* tex = anim.texture.get();
        if (!prog || !tex) {
            batch.dirty = true;
            return;
        }

        batch.prog = prog;
        batch.key.shader = static_cast<unsigned int>(*prog);
        batch.key.atlas = tex->get_id();
        batch.layers = kLayer;
        batch.key.layers = kLayer;
        batch.atlas_ref = anim.texture.shared();

        for (int display_dir = 0; display_dir < anim.display_direction_count; ++display_dir) {
            const int source_dir = display_dir * static_cast<int>(anim.directions.size()) / anim.display_direction_count;
            const auto& frames = anim.directions[static_cast<std::size_t>(source_dir)];
            if (frames.empty()) continue;
            const int frame_idx = static_cast<int>(state->time * anim.fps) % static_cast<int>(frames.size());
            push_frame(batch, frames[static_cast<std::size_t>(frame_idx)], panel_anchor(display_dir, *win), state->scale);
        }

        batch.dirty = true;
    }
}

int main()
{
    fs::path root = find_res_root();
    if (root.empty()) {
        std::printf("[aoe2de_spearman] Cannot find gld res directory from current path.\n");
        std::fflush(stdout);
        return 1;
    }
    gld::ResMgrWithGlslPreProcess::create_instance(root);
    gld::DefResMgr::create_instance(root);

    DemoState initial;
    if (!load_manifest(root, initial)) {
        std::printf("[aoe2de_spearman] Missing converted cache.\n");
        std::printf("Run:\n");
        std::printf("python E:\\code\\gld\\tools\\aoe2de_export\\aoe2de_export.py --aoe2 \"D:\\program1\\steam\\steamapps\\common\\AoE2DE\" --out \"E:\\code\\gld\\res\\aoe2de_cache\\spearman\" --unit spearman --scale auto --directions 32\n");
        std::fflush(stdout);
        return 1;
    }

    App app;
    app.add_plugin(WindowPlugin{ 1280, 720, "aoe2de_spearman - converted local AoE2DE graphics" });
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root));
    app.add_plugin(AssetPlugin);
    app.add_plugin(CorePlugin);
    app.add_plugin(InputPlugin);
    app.add_plugin(RenderPlugin);

    app.world.add_resource<DemoState>(std::move(initial));

    app.add_system(Stage::Startup, [](EcsWorld& w) {
        auto& state = w.resource<DemoState>();
        auto& srv = w.resource<AssetServer>();
        auto& reg = w.reg();

        state.shader = srv.load_program("ecs/text_vs.glsl", "ecs/aoe2_sprite_fg.glsl");
        for (auto& anim : state.animations)
            anim.texture = srv.load_texture(anim.texture_path, Channels::RGBA, false, false);

        state.camera = w.spawn();
        Camera cam;
        cam.kind = CameraKind::Ortho;
        cam.priority = 0;
        cam.target = 0;
        cam.layers = kLayer;
        cam.clear_color = glm::vec4(0.06f, 0.07f, 0.09f, 1.f);
        reg.emplace<Camera>(state.camera, cam);
        emplace_render_passes<BatchPass>(w, state.camera);

        state.batch = w.spawn();
        auto& batch = reg.emplace<BatchComponent>(state.batch);
        batch.layers = kLayer;
        batch.key.layers = kLayer;

        std::printf("[aoe2de_spearman] 1 idle, 2 walk, 3 attack, 4 death, Space restart\n");
    });

    app.add_system(Stage::Update, input_system);
    app.add_system(Stage::PostUpdate, render_spearman_system);

    run_app(app);
    return 0;
}
