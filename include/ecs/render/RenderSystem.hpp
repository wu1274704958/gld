#pragma once

// Multi-camera ECS render pipeline (modern GL, Vulkan-shaped).
//
//   spawn_camera_system    (First)      – allocate contiguous, gap-filling ids
//   camera_matrices_system (PostUpdate) – projection per CameraKind
//   render_system          (Render)     – passes ordered by priority: bind target
//                                          (FBO or window), layer-filtered draws,
//                                          then insert WaitPresent on the primary
//                                          window camera
//   present_system         (Last)       – wait the fence, swap (present command)
//
// GL lives in EcsRender.cpp.

#include "RenderComponents.hpp"
#include "RenderGraph.hpp"
#include "RenderPassExec.hpp"
#include "../App.hpp"
#include "../EcsWorld.hpp"

#include <type_traits>

namespace gld::ecs {

    using DefaultRenderPassRegistry = RenderPassComponentRegistry<
        RenderPasses<MeshPass>,
        RenderPasses<BatchPass>,
        RenderPasses<FullscreenRenderPass>,
        RenderPasses<MeshPass, BatchPass>,
        RenderPasses<BatchPass, MeshPass>
    >;

    template<IRenderPass... Passes>
    RenderPasses<Passes...>& emplace_render_passes(EcsWorld& w, entt::entity e) {
        using PassComponent = RenderPasses<Passes...>;
        if (auto* existing = w.reg().try_get<PassComponent>(e)) {
            *existing = PassComponent{};
            return *existing;
        }
        return w.reg().emplace<PassComponent>(e);
    }

    template<IRenderPass... Passes>
    RenderPasses<Passes...>& emplace_render_passes(App& app, entt::entity e) {
        return emplace_render_passes<Passes...>(app.world, e);
    }

    void spawn_camera_system(EcsWorld& w);
    void camera_matrices_system(EcsWorld& w);
    void render_graph_sync_system(EcsWorld& w);
    void render_graph_sort_system(EcsWorld& w);
    void render_system(EcsWorld& w);
    void render_system_default(EcsWorld& w);
    void present_system(EcsWorld& w);
    void cleanup_render_common_resources(EcsWorld& w);
    void cleanup_render_resources(EcsWorld& w);

    template<class Registry>
    void cleanup_render_resources_t(EcsWorld& w) {
        cleanup_registered_render_passes<Registry>(w);
        cleanup_render_common_resources(w);
    }

    // Registers all of the above (2D/text passes require a BatchPlugin).
    void RenderPlugin(App& app);
}
