#pragma once

// Offscreen render target (FBO + colour texture + depth/stencil renderbuffer).
// Cameras with target != 0 render here; the colour texture can be sampled by a
// later (higher-priority) camera pass — that read-after-write is the render
// dependency (GL tracks the hazard implicitly by pass ordering).

#include <memory>
#include <texture.hpp>

namespace gld::ecs {

    struct RenderTarget {
        unsigned int fbo = 0;
        unsigned int depth_rbo = 0;
        std::shared_ptr<Texture<TexType::D2>> color;
        int width = 0;
        int height = 0;
    };

    // Create an RGBA8 colour + DEPTH24_STENCIL8 offscreen target. Main-thread/GL.
    std::shared_ptr<RenderTarget> create_render_target(int width, int height);
}
