#include <glad/glad.h>
#include <ecs/render/RenderTarget.hpp>
#include <texture.hpp>
#include <cstdio>

namespace gld::ecs {

    std::shared_ptr<RenderTarget> create_render_target(int width, int height) {
        auto rt = std::make_shared<RenderTarget>();
        rt->width = width;
        rt->height = height;

        rt->color = std::make_shared<Texture<TexType::D2>>();
        rt->color->create();
        rt->color->bind();
        rt->color->tex_image(0, GL_RGBA, 0, GL_RGBA,
                             static_cast<const unsigned char*>(nullptr), width, height);
        rt->color->set_paramter<TexOption::MIN_FILTER, TexOpVal::LINEAR>();
        rt->color->set_paramter<TexOption::MAG_FILTER, TexOpVal::LINEAR>();
        rt->color->set_paramter<TexOption::WRAP_S, TexOpVal::CLAMP_TO_EDGE>();
        rt->color->set_paramter<TexOption::WRAP_T, TexOpVal::CLAMP_TO_EDGE>();
        rt->color->measure.width = width;
        rt->color->measure.height = height;
        rt->color->unbind();

        glGenRenderbuffers(1, &rt->depth_rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, rt->depth_rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glGenFramebuffers(1, &rt->fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, rt->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               rt->color->get_id(), 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, rt->depth_rbo);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::fprintf(stderr, "[RenderTarget] incomplete FBO %ux%u\n", width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return rt;
    }
}
