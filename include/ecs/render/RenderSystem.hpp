#pragma once

// ECS render systems: build a mesh, compute the camera matrices, and draw all
// (GlobalTransform, MeshHandle, Material) entities. GL lives in EcsRender.cpp.

#include "RenderComponents.hpp"
#include "../App.hpp"
#include "../EcsWorld.hpp"

namespace gld::ecs {

    void camera_system(EcsWorld& w);      // projection (window aspect) + view (orbit)
    void render_system(EcsWorld& w);      // draw pass (clears + depth test)

    void RenderPlugin(App& app);          // Camera resource + camera_system + render_system
}
