#include <glad/glad.h>
#include <ecs/render/RenderTarget.hpp>
#include <texture.hpp>
#include <cstdio>
#include <utility>

namespace gld::ecs {

    void destroy_render_target_gpu(RenderTarget& rt) {
        if (rt.color) {
            rt.color->clean();
        }
        if (rt.depth) {
            rt.depth->clean();
        }
        if (rt.depth_rbo != 0) {
            glDeleteRenderbuffers(1, &rt.depth_rbo);
            rt.depth_rbo = 0;
        }
        if (rt.fbo != 0) {
            glDeleteFramebuffers(1, &rt.fbo);
            rt.fbo = 0;
        }
    }

    RenderTarget::~RenderTarget() {
        destroy_render_target_gpu(*this);
    }

    RenderTarget::RenderTarget(RenderTarget&& other) noexcept {
        *this = std::move(other);
    }

    RenderTarget& RenderTarget::operator=(RenderTarget&& other) noexcept {
        if (this == &other) return *this;

        destroy_render_target_gpu(*this);
        fbo = other.fbo;
        depth_rbo = other.depth_rbo;
        color = std::move(other.color);
        depth = std::move(other.depth);
        width = other.width;
        height = other.height;

        other.fbo = 0;
        other.depth_rbo = 0;
        other.width = 0;
        other.height = 0;
        return *this;
    }

    void resize_render_target(RenderTarget& rt, int width, int height) {
        destroy_render_target_gpu(rt);

        rt.width = width;
        rt.height = height;

        if (!rt.color)
            rt.color = std::make_shared<Texture<TexType::D2>>();
        else
            rt.color->clean();
        rt.color->create();
        rt.color->bind();
        rt.color->tex_image(0, GL_RGBA, 0, GL_RGBA,
                             static_cast<const unsigned char*>(nullptr), width, height);
        rt.color->set_paramter<TexOption::MIN_FILTER, TexOpVal::LINEAR>();
        rt.color->set_paramter<TexOption::MAG_FILTER, TexOpVal::LINEAR>();
        rt.color->set_paramter<TexOption::WRAP_S, TexOpVal::CLAMP_TO_EDGE>();
        rt.color->set_paramter<TexOption::WRAP_T, TexOpVal::CLAMP_TO_EDGE>();
        rt.color->measure.width = width;
        rt.color->measure.height = height;
        rt.color->unbind();

        if (!rt.depth)
            rt.depth = std::make_shared<Texture<TexType::D2>>();
        else
            rt.depth->clean();
        rt.depth->create();
        rt.depth->bind();
        rt.depth->tex_image(0, GL_DEPTH_COMPONENT24, 0, GL_DEPTH_COMPONENT,
                            static_cast<const float*>(nullptr), width, height);
        rt.depth->set_paramter<TexOption::MIN_FILTER, TexOpVal::NEAREST>();
        rt.depth->set_paramter<TexOption::MAG_FILTER, TexOpVal::NEAREST>();
        rt.depth->set_paramter<TexOption::WRAP_S, TexOpVal::CLAMP_TO_EDGE>();
        rt.depth->set_paramter<TexOption::WRAP_T, TexOpVal::CLAMP_TO_EDGE>();
        rt.depth->measure.width = width;
        rt.depth->measure.height = height;
        rt.depth->unbind();

        glGenFramebuffers(1, &rt.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               rt.color->get_id(), 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                               rt.depth->get_id(), 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::fprintf(stderr, "[RenderTarget] incomplete FBO %ux%u\n", width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    std::shared_ptr<RenderTarget> create_render_target(int width, int height) {
        auto rt = std::make_shared<RenderTarget>();
        resize_render_target(*rt, width, height);
        return rt;
    }
}
