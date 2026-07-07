#pragma once

// motion_trail.h – selective per-object motion-trail (afterimage) post-process.
//
//  Runs AFTER bloom: composites a fading "tail" of the trail-enabled objects on
//  top of the already-bloomed final image, without touching any other object.
//
//  Usage (inside RenderDemo::draw(), AFTER bloom.end_and_present()):
//      motion_trail.render(width, height, [&]{
//          for (auto& n : trail_objs) n->draw();   // render only trail objects
//      });
//
//  On window resize: motion_trail.resize(w, h);
//
//  Pipeline per frame:
//    1) render trail objects into an isolated RGB16F FBO (current frame)
//    2) composite TAIL (accum_prev * decay * strength) additively onto the FBO
//       bound on entry (the post-bloom screen) — the current head is NOT added
//       here (it is already provided, bloomed, by the main scene pass), so the
//       head is never doubled.
//    3) update persistent accumulation: accum = max(current, accum_prev*decay)
//
//  accum = max(current, prev*decay) is a stable phosphor/echo: static bright
//  pixels don't build up, moving pixels leave fading ghosts.

#include <glad/glad.h>
#include <data_mgr.hpp>
#include <functional>
#include <memory>

namespace gld {

class MotionTrailPipeline {
public:
    // Tunable parameters
    float decay    = 0.72f;  // tail fade per frame [0,1) — larger = longer trail
    float strength = 0.42f;   // trail composite strength

    void create(int w, int h)
    {
        width_ = w; height_ = h;

        accum_prog = DefDataMgr::instance()->load<DataType::Program>(
            "framebuffer/vert.glsl", "trail/accum_fg.glsl");
        accum_prog->use();
        accum_prog->locat_uniforms("curr", "prev", "decay");
        glUniform1i(accum_prog->uniform_id("curr"), 0);
        glUniform1i(accum_prog->uniform_id("prev"), 1);

        present_prog = DefDataMgr::instance()->load<DataType::Program>(
            "framebuffer/vert.glsl", "trail/present_fg.glsl");
        present_prog->use();
        present_prog->locat_uniforms("image", "factor");
        glUniform1i(present_prog->uniform_id("image"), 0);
        present_prog->unuse();

        create_quad();
        create_targets();
    }

    void resize(int w, int h)
    {
        if (w <= 0 || h <= 0) return;
        width_ = w; height_ = h;
        destroy_targets();
        create_targets();
    }

    // Render trail objects and composite their fading tail onto the currently
    // bound framebuffer (expected to be the post-bloom screen).
    void render(int w, int h, const std::function<void()>& draw_objs)
    {
        GLint prev_fbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);

        // --- 1) isolated render of trail objects (current frame) ------------
        glBindFramebuffer(GL_FRAMEBUFFER, v5_fbo_);
        glViewport(0, 0, width_, height_);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        draw_objs();

        // --- 2) composite TAIL (accum_prev*decay*strength) onto screen ------
        glDisable(GL_DEPTH_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
        glViewport(0, 0, w, h);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        present_prog->use();
        glUniform1f(present_prog->uniform_id("factor"), decay * strength);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, accum_tex_[src_]);
        draw_quad();
        glDisable(GL_BLEND);

        // --- 3) update persistent accumulation ------------------------------
        int dst = 1 - src_;
        glViewport(0, 0, width_, height_);
        glBindFramebuffer(GL_FRAMEBUFFER, accum_fbo_[dst]);
        glClear(GL_COLOR_BUFFER_BIT);
        accum_prog->use();
        glUniform1f(accum_prog->uniform_id("decay"), decay);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, v5_tex_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, accum_tex_[src_]);
        draw_quad();
        src_ = dst;

        // --- restore ---------------------------------------------------------
        present_prog->unuse();
        glActiveTexture(GL_TEXTURE0);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
        glViewport(0, 0, w, h);
    }

    ~MotionTrailPipeline()
    {
        destroy_targets();
        if (quad_vao_) glDeleteVertexArrays(1, &quad_vao_);
        if (quad_vbo_) glDeleteBuffers(1, &quad_vbo_);
    }

private:
    void create_quad()
    {
        float quad[] = {
            -1.f,  1.f, 0.f, 1.f,
            -1.f, -1.f, 0.f, 0.f,
             1.f, -1.f, 1.f, 0.f,
            -1.f,  1.f, 0.f, 1.f,
             1.f, -1.f, 1.f, 0.f,
             1.f,  1.f, 1.f, 1.f
        };
        glGenVertexArrays(1, &quad_vao_);
        glGenBuffers(1, &quad_vbo_);
        glBindVertexArray(quad_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
    }

    void draw_quad()
    {
        glBindVertexArray(quad_vao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    GLuint make_color_tex()
    {
        GLuint t;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width_, height_, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return t;
    }

    void create_targets()
    {
        // Isolated trail-object FBO: RGB16F + depth/stencil renderbuffer.
        glGenFramebuffers(1, &v5_fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, v5_fbo_);
        v5_tex_ = make_color_tex();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, v5_tex_, 0);
        glGenRenderbuffers(1, &depth_rbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width_, height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_rbo_);

        // Persistent ping-pong accumulation FBOs (colour only), cleared once.
        glGenFramebuffers(2, accum_fbo_);
        for (int i = 0; i < 2; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, accum_fbo_[i]);
            accum_tex_[i] = make_color_tex();
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accum_tex_[i], 0);
            glClearColor(0.f, 0.f, 0.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        src_ = 0;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void destroy_targets()
    {
        if (v5_tex_)    { glDeleteTextures(1, &v5_tex_); v5_tex_ = 0; }
        if (depth_rbo_) { glDeleteRenderbuffers(1, &depth_rbo_); depth_rbo_ = 0; }
        if (v5_fbo_)    { glDeleteFramebuffers(1, &v5_fbo_); v5_fbo_ = 0; }
        for (int i = 0; i < 2; ++i) {
            if (accum_tex_[i]) { glDeleteTextures(1, &accum_tex_[i]); accum_tex_[i] = 0; }
            if (accum_fbo_[i]) { glDeleteFramebuffers(1, &accum_fbo_[i]); accum_fbo_[i] = 0; }
        }
    }

    int width_ = 0, height_ = 0;

    GLuint v5_fbo_ = 0, v5_tex_ = 0, depth_rbo_ = 0;
    GLuint accum_fbo_[2] = {0, 0};
    GLuint accum_tex_[2] = {0, 0};
    int    src_ = 0;                 // persistent: index of latest accumulation
    GLuint quad_vao_ = 0, quad_vbo_ = 0;

    std::shared_ptr<Program> accum_prog, present_prog;
};

} // namespace gld
