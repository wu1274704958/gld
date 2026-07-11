#pragma once

// Offscreen render target (FBO + colour texture + depth/stencil renderbuffer).
// Cameras with target != 0 render here; the colour texture can be sampled by a
// later (higher-priority) camera pass — that read-after-write is the render
// dependency (GL tracks the hazard implicitly by pass ordering).

#include <memory>
#include <texture.hpp>

namespace gld::ecs {

    struct RenderTarget {
        RenderTarget() = default;
        ~RenderTarget();
        RenderTarget(const RenderTarget&) = delete;
        RenderTarget& operator=(const RenderTarget&) = delete;
        RenderTarget(RenderTarget&& other) noexcept;
        RenderTarget& operator=(RenderTarget&& other) noexcept;

        unsigned int fbo = 0;
        unsigned int depth_rbo = 0;
        std::shared_ptr<Texture<TexType::D2>> color;
        std::shared_ptr<Texture<TexType::D2>> depth;
        int width = 0;
        int height = 0;
    };

    // Create an RGBA8 colour + DEPTH24_STENCIL8 offscreen target. Main-thread/GL.
    std::shared_ptr<RenderTarget> create_render_target(int width, int height);
    void resize_render_target(RenderTarget& rt, int width, int height);
    void destroy_render_target_gpu(RenderTarget& rt);
}
