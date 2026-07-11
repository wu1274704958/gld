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
#include "../App.hpp"
#include "../EcsWorld.hpp"

namespace gld::ecs {

    void spawn_camera_system(EcsWorld& w);
    void camera_matrices_system(EcsWorld& w);
    void render_graph_sync_system(EcsWorld& w);
    void render_graph_sort_system(EcsWorld& w);
    void render_system(EcsWorld& w);
    void present_system(EcsWorld& w);
    void cleanup_render_resources(EcsWorld& w);

    // Registers all of the above (2D/text passes require a BatchPlugin).
    void RenderPlugin(App& app);
}
