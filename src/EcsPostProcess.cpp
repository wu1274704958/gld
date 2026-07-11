#include <ecs/render/PostProcess.hpp>

#include <algorithm>
#include <utility>

#include <ecs/Window.hpp>
#include <ecs/assets/AssetServer.hpp>

namespace gld::ecs {

    PostProcessBuilder::PostProcessBuilder(EcsWorld& world, entt::entity source, std::uint64_t owner,
                                           int width, int height, unsigned int final_target,
                                           glm::ivec2 final_size, int base_priority, bool final_process,
                                           PostProcessInput source_input)
        : world_(world),
          source_(source),
          owner_(owner),
          width_(width),
          height_(height),
          final_target_(final_target),
          final_size_(final_size),
          base_priority_(base_priority),
          final_process_(final_process),
          source_input_(std::move(source_input)) {}

    PostProcessOutput PostProcessBuilder::add_pass(
        const std::string& name,
        const std::string& fragment_shader,
        const std::vector<PostProcessTextureInput>& inputs,
        const std::vector<FullscreenUniform>& uniforms,
        PostProcessOutputMode mode) {

        auto& reg = world_.reg();
        auto& graph = world_.resource_or_add<RenderGraphResource>();
        auto& srv = world_.resource<AssetServer>();

        std::shared_ptr<RenderTarget> rt;
        unsigned int target = final_target_;
        glm::ivec2 target_size = final_size_;
        std::shared_ptr<Texture<TexType::D2>> output;

        if (mode == PostProcessOutputMode::Offscreen) {
            rt = create_render_target(width_, height_);
            target = rt->fbo;
            target_size = glm::ivec2(width_, height_);
            output = rt->color;
            created_targets.push_back(rt);
        }

        entt::entity e = world_.spawn();
        Camera cam;
        cam.kind = CameraKind::Ortho;
        cam.priority = base_priority_ + (++seq_);
        cam.target = target;
        cam.target_size = target_size;
        cam.pass_mask = RenderPassFullscreen;
        cam.do_clear = true;
        cam.clear_color = glm::vec4(0.f, 0.f, 0.f, 1.f);
        reg.emplace<Camera>(e, cam);

        FullscreenPass pass;
        pass.shader = srv.load_sync(ProgramDesc("ecs/post_fullscreen_vs.glsl", fragment_shader, "", "", ""));
        pass.debug_name = name;
        pass.uniforms = uniforms;
        for (const auto& input : inputs) {
            pass.textures.push_back(FullscreenTextureSlot{ input.uniform, input.input.color });
            if (input.input.producer != 0)
                graph.add_edge(input.input.producer, render_graph_camera_node_id(e));
        }
        reg.emplace<FullscreenPass>(e, std::move(pass));

        const RenderGraphNodeId node_id = render_graph_camera_node_id(e);
        graph.nodes.push_back(RenderGraphNode{
            node_id, RenderGraphNodeKind::Camera, e, cam.priority, true, true, owner_, name
        });
        graph.camera_nodes[e] = node_id;
        graph.rebuild_index();
        graph.dirty = true;

        created_entities.push_back(e);

        return PostProcessOutput{
            output,
            rt ? rt->depth : nullptr,
            node_id,
            mode == PostProcessOutputMode::Offscreen ? width_ : target_size.x,
            mode == PostProcessOutputMode::Offscreen ? height_ : target_size.y,
            inputs.empty() ? 0.1f : inputs.front().input.near_z,
            inputs.empty() ? 200.f : inputs.front().input.far_z
        };
    }

    glm::ivec2 PostProcessManager::target_size_for(entt::entity source_camera) const {
        if (!world) return glm::ivec2(1, 1);
        auto& reg = world->reg();
        if (auto state_it = sources_.find(source_camera); state_it != sources_.end() && state_it->second.redirected) {
            const glm::ivec2 original = state_it->second.original_target_size;
            if (original.x > 0 && original.y > 0)
                return original;
            if (auto* win = world->try_resource<Window>())
                return glm::ivec2(win->width, win->height);
            return glm::ivec2(1, 1);
        }
        if (auto* cam = reg.try_get<Camera>(source_camera)) {
            if (cam->target_size.x > 0 && cam->target_size.y > 0)
                return cam->target_size;
        }
        if (auto* win = world->try_resource<Window>())
            return glm::ivec2(win->width, win->height);
        return glm::ivec2(1, 1);
    }

    void PostProcessManager::clear_generated(entt::entity source_camera, bool restore_source) {
        if (!world) return;
        auto state_it = sources_.find(source_camera);
        if (state_it == sources_.end()) return;

        auto& reg = world->reg();
        auto& graph = world->resource_or_add<RenderGraphResource>();
        auto& state = state_it->second;

        for (auto e : state.generated_entities) {
            RenderGraphNodeId id = render_graph_camera_node_id(e);
            graph.remove_edges_for(id);
            graph.camera_nodes.erase(e);
            graph.nodes.erase(std::remove_if(graph.nodes.begin(), graph.nodes.end(),
                [&](const RenderGraphNode& node) { return node.id == id; }), graph.nodes.end());
            if (reg.valid(e)) reg.destroy(e);
        }
        state.generated_entities.clear();
        state.generated_targets.clear();

        if (restore_source && state.redirected && reg.valid(source_camera)) {
            if (auto* cam = reg.try_get<Camera>(source_camera)) {
                cam->target = state.original_target;
                cam->target_size = state.original_target_size;
            }
            state.source_target.reset();
            state.redirected = false;
        }

        graph.rebuild_index();
        graph.dirty = true;
    }

    void PostProcessManager::rebuild(entt::entity source_camera) {
        if (!world) return;
        auto& reg = world->reg();
        auto* cam = reg.try_get<Camera>(source_camera);
        if (!cam) return;

        clear_generated(source_camera, false);

        auto& state = sources_[source_camera];
        if (!state.redirected) {
            state.original_target = cam->target;
            state.original_target_size = cam->target_size;
            state.redirected = true;
        }

        const glm::ivec2 size = target_size_for(source_camera);
        if (!state.source_target || state.source_target->width != size.x || state.source_target->height != size.y)
            state.source_target = create_render_target(size.x, size.y);

        cam->target = state.source_target->fbo;
        cam->target_size = glm::ivec2(size.x, size.y);

        PostProcessInput input{
            state.source_target->color,
            state.source_target->depth,
            render_graph_camera_node_id(source_camera),
            size.x,
            size.y,
            cam->near_z,
            cam->far_z
        };
        const int base_priority = cam->priority;

        auto ids_it = source_entries_.find(source_camera);
        if (ids_it == source_entries_.end()) return;

        std::vector<std::uint64_t> enabled_ids;
        for (auto id : ids_it->second) {
            auto it = entries_.find(id);
            if (it != entries_.end() && it->second.enabled)
                enabled_ids.push_back(id);
        }

        if (enabled_ids.empty()) {
            clear_generated(source_camera, true);
            return;
        }

        for (std::size_t i = 0; i < enabled_ids.size(); ++i) {
            auto& entry = entries_[enabled_ids[i]];
            PostProcessBuilder builder(
                *world, source_camera, entry.handle.value, size.x, size.y,
                state.original_target, state.original_target_size,
                base_priority, i + 1 == enabled_ids.size(), input);
            input = entry.build(builder, input);
            state.generated_entities.insert(state.generated_entities.end(),
                builder.created_entities.begin(), builder.created_entities.end());
            state.generated_targets.insert(state.generated_targets.end(),
                builder.created_targets.begin(), builder.created_targets.end());
        }
    }

    void PostProcessManager::remove(PostProcessHandle handle) {
        auto it = entries_.find(handle.value);
        if (it == entries_.end()) return;
        const entt::entity source = it->second.source;
        entries_.erase(it);
        auto& ids = source_entries_[source];
        ids.erase(std::remove(ids.begin(), ids.end(), handle.value), ids.end());
        if (ids.empty()) {
            clear_generated(source, true);
            source_entries_.erase(source);
            sources_.erase(source);
        } else {
            rebuild(source);
        }
    }

    void PostProcessManager::clear(entt::entity source_camera) {
        auto ids_it = source_entries_.find(source_camera);
        if (ids_it != source_entries_.end()) {
            for (auto id : ids_it->second) entries_.erase(id);
            source_entries_.erase(ids_it);
        }
        clear_generated(source_camera, true);
        sources_.erase(source_camera);
    }

    void PostProcessManager::set_enabled(PostProcessHandle handle, bool enabled) {
        auto it = entries_.find(handle.value);
        if (it == entries_.end()) return;
        if (it->second.enabled == enabled) return;
        it->second.enabled = enabled;
        rebuild(it->second.source);
    }

    void PostProcessManager::update_sizes() {
        std::vector<entt::entity> sources;
        sources.reserve(source_entries_.size());
        for (const auto& [source, ids] : source_entries_) sources.push_back(source);
        for (auto source : sources) {
            auto state_it = sources_.find(source);
            if (state_it == sources_.end() || !state_it->second.source_target) continue;
            const glm::ivec2 size = target_size_for(source);
            if (state_it->second.source_target->width != size.x ||
                state_it->second.source_target->height != size.y) {
                rebuild(source);
            }
        }
    }

    void PostProcessManager::cleanup() {
        std::vector<entt::entity> sources;
        sources.reserve(sources_.size());
        for (const auto& [source, state] : sources_) sources.push_back(source);
        for (auto source : sources) clear(source);
    }

    void post_process_update_system(EcsWorld& w) {
        auto& mgr = w.resource_or_add<PostProcessManager>();
        mgr.world = &w;
        mgr.update_sizes();
    }

    void PostProcessPlugin(App& app) {
        auto& mgr = app.world.resource_or_add<PostProcessManager>();
        mgr.world = &app.world;
        app.add_system(Stage::PreUpdate, post_process_update_system);
    }
}
