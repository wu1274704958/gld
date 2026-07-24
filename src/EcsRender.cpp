#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <vector>
#include <set>
#include <climits>
#include <unordered_map>

#include <ecs/render/RenderSystem.hpp>
#include <ecs/PerformanceMonitoring.hpp>
#include <ecs/render/BatchSystem.hpp>   // draw_batches
#include <ecs/render/RenderTarget.hpp>
#include <ecs/render/PostProcess.hpp>
#include <ecs/render/Lighting.hpp>
#include <ecs/render/RenderPassExec.hpp>
#include <ecs/Components.hpp>
#include <ecs/Window.hpp>

namespace gld::ecs {

    bool register_render_pass(EcsWorld& world, RegisteredRenderPassId id,
                              RegisteredRenderPassHandler handler) {
        if (id == 0 || !handler.render) return false;
        auto& registry = world.resource_or_add<RegisteredRenderPassRegistry>();
        registry.handlers.insert_or_assign(id, std::move(handler));
        return true;
    }

    void unregister_render_pass(EcsWorld& world, RegisteredRenderPassId id) {
        if (auto* registry = world.try_resource<RegisteredRenderPassRegistry>())
            registry->handlers.erase(id);
    }

    // Allocate contiguous, gap-filling ids to any camera with id < 0. Ids of
    // destroyed cameras disappear from the live set, so the next spawn reuses
    // the smallest free id.
    void spawn_camera_system(EcsWorld& w) {
        auto& reg = w.reg();
        std::set<int> used;
        for (auto e : reg.view<Camera>()) {
            int id = reg.get<Camera>(e).id;
            if (id >= 0) used.insert(id);
        }
        for (auto e : reg.view<Camera>()) {
            auto& c = reg.get<Camera>(e);
            if (c.id < 0) {
                int n = 0;
                while (used.count(n)) ++n;
                c.id = n;
                used.insert(n);
            }
        }
    }

    void camera_matrices_system(EcsWorld& w) {
        auto& reg = w.reg();
        int ww = 1, wh = 1;
        if (auto* win = w.try_resource<Window>()) { ww = win->width; wh = win->height; }

        for (auto e : reg.view<Camera>()) {
            auto& c = reg.get<Camera>(e);
            int tw = c.target_size.x > 0 ? c.target_size.x : ww;
            int th = c.target_size.y > 0 ? c.target_size.y : wh;

            if (c.kind == CameraKind::Perspective) {
                float aspect = th > 0 ? static_cast<float>(tw) / static_cast<float>(th) : 1.f;
                c.projection = glm::perspective(glm::radians(c.fov), aspect, c.near_z, c.far_z);
            } else {
                float W = static_cast<float>(tw), H = static_cast<float>(th);
                c.projection = glm::ortho(-W * 0.5f, W * 0.5f, -H * 0.5f, H * 0.5f, -4000.f, 4000.f);
            }
        }
    }

    static bool render_graph_node_less(const RenderGraphResource& graph,
                                       RenderGraphNodeId a,
                                       RenderGraphNodeId b) {
        const RenderGraphNode* na = graph.find(a);
        const RenderGraphNode* nb = graph.find(b);
        if (!na || !nb) return a < b;
        if (na->priority != nb->priority) return na->priority < nb->priority;
        return na->id < nb->id;
    }

    void render_graph_sync_system(EcsWorld& w) {
        auto& reg = w.reg();
        auto& graph = w.resource_or_add<RenderGraphResource>();
        graph.diagnostics.begin_frame();

        bool changed = false;
        for (auto e : reg.view<Camera>()) {
            auto& cam = reg.get<Camera>(e);
            const RenderGraphNodeId id = render_graph_camera_node_id(e);
            if (!graph.find(id)) {
                graph.nodes.push_back(RenderGraphNode{
                    id, RenderGraphNodeKind::Camera, e, cam.priority, true, false, 0, "Camera"
                });
                graph.camera_nodes[e] = id;
                changed = true;
                continue;
            }

            auto& node = *graph.find(id);
            if (node.entity != e || node.priority != cam.priority || !node.enabled) {
                node.entity = e;
                node.priority = cam.priority;
                node.enabled = true;
                changed = true;
            }
            graph.camera_nodes[e] = id;
        }

        const auto old_size = graph.nodes.size();
        graph.nodes.erase(std::remove_if(graph.nodes.begin(), graph.nodes.end(),
            [&](const RenderGraphNode& node) {
                if (node.kind != RenderGraphNodeKind::Camera) return false;
                if (node.entity == entt::null || !reg.valid(node.entity) ||
                    !reg.try_get<Camera>(node.entity)) {
                    graph.camera_nodes.erase(node.entity);
                    return true;
                }
                return false;
            }), graph.nodes.end());
        if (graph.nodes.size() != old_size) changed = true;

        if (changed) {
            graph.rebuild_index();
            graph.dirty = true;
        } else if (graph.index.size() != graph.nodes.size()) {
            graph.rebuild_index();
        }
    }

    void render_graph_sort_system(EcsWorld& w) {
        auto& graph = w.resource_or_add<RenderGraphResource>();
        if (!graph.dirty && !graph.execution_order.empty()) {
            graph.diagnostics.nodes = static_cast<std::uint32_t>(graph.nodes.size());
            graph.diagnostics.edges = static_cast<std::uint32_t>(graph.edges.size());
            return;
        }

        graph.rebuild_index();
        graph.execution_order.clear();
        graph.diagnostics.nodes = static_cast<std::uint32_t>(graph.nodes.size());
        graph.diagnostics.edges = static_cast<std::uint32_t>(graph.edges.size());

        std::unordered_map<RenderGraphNodeId, std::uint32_t> indegree;
        std::unordered_map<RenderGraphNodeId, std::vector<RenderGraphNodeId>> outgoing;

        for (const auto& node : graph.nodes) {
            if (node.enabled) indegree[node.id] = 0;
        }

        for (const auto& edge : graph.edges) {
            const auto from_it = indegree.find(edge.from);
            const auto to_it = indegree.find(edge.to);
            if (from_it == indegree.end() || to_it == indegree.end()) {
                ++graph.diagnostics.skipped_invalid;
                continue;
            }
            outgoing[edge.from].push_back(edge.to);
            ++to_it->second;
        }

        std::vector<RenderGraphNodeId> ready;
        ready.reserve(indegree.size());
        for (const auto& [id, degree] : indegree) {
            if (degree == 0) ready.push_back(id);
        }
        std::sort(ready.begin(), ready.end(),
            [&](RenderGraphNodeId a, RenderGraphNodeId b) {
                return render_graph_node_less(graph, a, b);
            });

        while (!ready.empty()) {
            RenderGraphNodeId id = ready.front();
            ready.erase(ready.begin());
            graph.execution_order.push_back(id);

            auto out_it = outgoing.find(id);
            if (out_it == outgoing.end()) continue;

            for (RenderGraphNodeId next : out_it->second) {
                auto degree_it = indegree.find(next);
                if (degree_it == indegree.end()) continue;
                if (--degree_it->second == 0) {
                    ready.push_back(next);
                    std::sort(ready.begin(), ready.end(),
                        [&](RenderGraphNodeId a, RenderGraphNodeId b) {
                            return render_graph_node_less(graph, a, b);
                        });
                }
            }
        }

        if (graph.execution_order.size() != indegree.size()) {
            ++graph.diagnostics.cycles;
        }

        graph.dirty = false;
    }

    static void render_diagnostics_begin_frame_system(EcsWorld& w) {
        w.resource_or_add<RenderDiagnostics>().begin_frame();
    }

    template<IRenderPass Pass>
    static void execute_tuple_pass(RenderPassContext& ctx, const Pass& pass) {
        ctx.state_cache.apply(resolve_render_pass_state(Pass::id, pass.state));
        RenderPassExecutor<Pass>::render(ctx, pass);
    }

    template<IRenderPassComponent PassComponent>
    static bool try_render_pass_component(EcsWorld& w, entt::entity e, const Camera& cam, RenderStateCache& state, int target_width, int target_height) {
        auto* component = w.reg().try_get<PassComponent>(e);
        if (!component) return false;

        RenderPassContext ctx{ w, e, cam, state, target_width, target_height };
        std::apply([&](const auto&... pass) {
            (execute_tuple_pass(ctx, pass), ...);
        }, component->passes);
        return true;
    }

    template<class... PassComponents>
    static bool try_render_registered_passes(RenderPassComponentRegistry<PassComponents...>,
                                             EcsWorld& w,
                                             entt::entity e,
                                             const Camera& cam,
                                             RenderStateCache& state,
                                             int target_width,
                                             int target_height) {
        return (try_render_pass_component<PassComponents>(w, e, cam, state, target_width, target_height) || ...);
    }

    template<class... PassComponents>
    static bool has_compile_time_passes(RenderPassComponentRegistry<PassComponents...>,
                                        EcsWorld& w, entt::entity e) {
        return (w.reg().template any_of<PassComponents>(e) || ...);
    }

    static bool try_render_runtime_passes(EcsWorld& w, entt::entity e,
                                          const Camera& cam, RenderStateCache& state,
                                          int target_width, int target_height) {
        const auto* passes = w.reg().try_get<RegisteredRenderPasses>(e);
        if (!passes) return false;
        auto& diagnostics = w.resource_or_add<RenderDiagnostics>();
        auto* registry = w.try_resource<RegisteredRenderPassRegistry>();
        RenderPassContext context{w, e, cam, state, target_width, target_height};
        for (const auto& pass : passes->passes) {
            const auto found = registry ? registry->handlers.find(pass.type)
                                        : decltype(registry->handlers.find(pass.type)){};
            if (!registry || found == registry->handlers.end() || !found->second.render) {
                GLD_PERF_MONITOR(++diagnostics.registered_pass_missing_handlers);
                continue;
            }
            const auto resolved = resolve_render_pass_state(
                found->second.default_state_pass_id, pass.state);
            state.apply(resolved);
            found->second.render(context, pass, resolved);
            GLD_PERF_MONITOR(++diagnostics.registered_passes);
        }
        return true;
    }

    template<class Registry>
    static void render_system_t(EcsWorld& w) {
        auto& reg = w.reg();
        auto& diag = w.resource_or_add<RenderDiagnostics>();
        auto& graph = w.resource_or_add<RenderGraphResource>();

        if (graph.dirty || graph.execution_order.empty())
            render_graph_sort_system(w);

        int ww = 1, wh = 1;
        if (auto* win = w.try_resource<Window>()) { ww = win->width; wh = win->height; }

        GLD_PERF_MONITOR(
            diag.cameras = graph.diagnostics.nodes;
            diag.graph_nodes = graph.diagnostics.nodes;
            diag.graph_edges = graph.diagnostics.edges;
            diag.graph_cycles = graph.diagnostics.cycles;
            diag.graph_skipped_invalid = graph.diagnostics.skipped_invalid;
        );
        RenderStateCache state;

        for (RenderGraphNodeId node_id : graph.execution_order) {
            const RenderGraphNode* node = graph.find(node_id);
            if (!node || !node->enabled || node->kind != RenderGraphNodeKind::Camera) {
                GLD_PERF_MONITOR(++diag.graph_skipped_invalid);
                continue;
            }

            entt::entity e = node->entity;
            if (e == entt::null || !reg.valid(e)) {
                GLD_PERF_MONITOR(++diag.graph_missing_cameras);
                continue;
            }
            const Camera* cam_ptr = reg.try_get<Camera>(e);
            if (!cam_ptr) {
                GLD_PERF_MONITOR(++diag.graph_missing_cameras);
                continue;
            }
            const Camera& cam = *cam_ptr;
            int tw = cam.target_size.x > 0 ? cam.target_size.x : ww;
            int th = cam.target_size.y > 0 ? cam.target_size.y : wh;

            state.bind_framebuffer(cam.target);
            state.viewport(tw, th);

            if (cam.do_clear) {
                glClearColor(cam.clear_color.r, cam.clear_color.g, cam.clear_color.b, cam.clear_color.a);
                // Clear must not inherit depth-write=false from a previous
                // batch/fullscreen pass; glClear respects the depth write mask.
                state.depth_write(true);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            }

            if (w.reg().any_of<RegisteredRenderPasses>(e)) {
                if (has_compile_time_passes(Registry{}, w, e))
                    GLD_PERF_MONITOR(++diag.registered_pass_component_conflicts);
                try_render_runtime_passes(w, e, cam, state, tw, th);
            } else if (!try_render_registered_passes(Registry{}, w, e, cam, state, tw, th)) {
                GLD_PERF_MONITOR(++diag.graph_skipped_invalid);
            }
            GLD_PERF_MONITOR(++diag.graph_executed);
        }
        state.bind_framebuffer(0);

        // Primary window camera = target 0 with the highest priority: it owns the
        // present. Insert/refresh a fence for present_system to wait on.
        entt::entity primary = entt::null;
        int best = INT_MIN;
        for (const auto& node : graph.nodes) {
            if (!node.enabled || node.kind != RenderGraphNodeKind::Camera) continue;
            entt::entity e = node.entity;
            if (e == entt::null || !reg.valid(e)) continue;
            const Camera* cam_ptr = reg.try_get<Camera>(e);
            if (!cam_ptr) continue;
            const Camera& c = *cam_ptr;
            if (c.target == 0 && c.priority >= best) { best = c.priority; primary = e; }
        }
        if (primary != entt::null) {
            GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            if (auto* wp = reg.try_get<WaitPresent>(primary)) {
                if (wp->fence) glDeleteSync(static_cast<GLsync>(wp->fence));
                wp->fence = fence;
            } else {
                reg.emplace<WaitPresent>(primary, WaitPresent{ static_cast<void*>(fence) });
            }
        }
    }

    void render_system_default(EcsWorld& w) {
        render_system_t<DefaultRenderPassRegistry>(w);
    }

    void render_system(EcsWorld& w) {
        render_system_default(w);
    }

    void present_system(EcsWorld& w) {
        auto& reg = w.reg();
        auto* win = w.try_resource<Window>();
        auto& settings = w.resource_or_add<RenderSettings>();
        auto& diag = w.resource_or_add<RenderDiagnostics>();
        GLFWwindow* handle = win ? win->handle : nullptr;

        std::vector<entt::entity> ents;
        for (auto e : reg.view<WaitPresent>()) ents.push_back(e);

        bool presented = false;
        for (auto e : ents) {
            auto& wp = reg.get<WaitPresent>(e);
            if (wp.fence) {
                if (settings.wait_for_present_fence) {
                    glClientWaitSync(static_cast<GLsync>(wp.fence), GL_SYNC_FLUSH_COMMANDS_BIT, 50000000ull);
                    GLD_PERF_MONITOR(++diag.present_fence_waits);
                }
                glDeleteSync(static_cast<GLsync>(wp.fence));
                wp.fence = nullptr;
            }
            if (handle && !presented) {
                GLD_PERF_TIME_POINT(present_started);
                glfwSwapBuffers(handle);
                GLD_PERF_MONITOR(
                    diag.present_ms += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - present_started).count();
                );
                presented = true;
                GLD_PERF_MONITOR(++diag.present_swaps);
            }
            reg.remove<WaitPresent>(e);
        }
        if (presented && win) win->presented = true;
    }

    void cleanup_render_common_resources(EcsWorld& w) {
        auto& reg = w.reg();
        std::vector<entt::entity> waits;
        for (auto e : reg.view<WaitPresent>()) waits.push_back(e);
        for (auto e : waits) {
            auto& wp = reg.get<WaitPresent>(e);
            if (wp.fence) {
                glDeleteSync(static_cast<GLsync>(wp.fence));
                wp.fence = nullptr;
            }
            reg.remove<WaitPresent>(e);
        }

        for (auto e : reg.view<BatchComponent>())
            destroy_batch_gpu(reg.get<BatchComponent>(e));

        if (auto* res = w.try_resource<BatchResources>())
            destroy_batch_resources_gpu(*res);

        if (auto* ppm = w.try_resource<PostProcessManager>())
            ppm->cleanup();

        if (auto* lighting = w.try_resource<LightingGpuResource>())
            lighting->cleanup();

        if (auto* rt = w.try_resource<std::shared_ptr<RenderTarget>>(); rt && *rt)
            destroy_render_target_gpu(**rt);
    }

    void cleanup_render_resources(EcsWorld& w) {
        if (auto* registry = w.try_resource<RegisteredRenderPassRegistry>()) {
            std::vector<std::function<void(EcsWorld&)>> cleanup_callbacks;
            cleanup_callbacks.reserve(registry->handlers.size());
            for (auto& [id, handler] : registry->handlers)
                if (handler.cleanup) cleanup_callbacks.push_back(handler.cleanup);
            for (auto& cleanup : cleanup_callbacks) cleanup(w);
        }
        cleanup_render_resources_t<DefaultRenderPassRegistry>(w);
    }

    void RenderPlugin(App& app) {
        app.world.resource_or_add<RenderSettings>();
        app.world.resource_or_add<RenderDiagnostics>();
        app.world.resource_or_add<RenderGraphResource>();
        app.world.resource_or_add<RegisteredRenderPassRegistry>();
        app.world.resource_or_add<FullscreenResources>();
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        app.add_system(Stage::First, render_diagnostics_begin_frame_system);
#endif
        app.add_system(Stage::First, spawn_camera_system);
        app.add_system(Stage::First, render_graph_sync_system);
        app.add_system(Stage::First, render_graph_sort_system);
        app.add_system(Stage::PostUpdate, camera_matrices_system);
        app.add_system(Stage::Render, render_system_default);
        app.add_system(Stage::Last, present_system);
        app.add_system(Stage::Shutdown, cleanup_render_resources);
    }
}
