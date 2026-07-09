#pragma once

// Factory for procedural base meshes (cube, and future quad/sphere/plane...).
// Declarations only; GL construction lives in EcsMeshFactory.cpp.

#include "RenderComponents.hpp"

namespace gld::ecs {

    struct MeshFactory {
        // Axis-aligned cube centered at origin. size = edge length (default 1.0).
        // 24 vertices (pos3 + uv2), 6 faces, GL_TRIANGLES.
        static MeshHandle cube(float size = 1.0f);

        // XY-plane quad centered at origin, facing +Z. size = edge length.
        // 4 vertices (pos3 + uv2), GL_TRIANGLES.
        static MeshHandle quad(float size = 1.0f);
    };
}
