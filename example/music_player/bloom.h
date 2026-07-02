#pragma once

// bloom.h – self-contained HDR bloom post-processing pipeline.
//
//  Usage (inside a RenderDemo::draw()):
//      bloom.begin();                 // bind HDR scene FBO + clear
//        ... render whole scene ...   // emissive colours > 1.0 will bloom
//      bloom.end_and_present();       // bright-pass → blur → composite to screen
//
//  On window resize: bloom.resize(w, h);
//
//  Pipeline:
//    scene → RGB16F FBO (+depth/stencil)
//          → bright-pass (luminance soft-knee threshold)
//          → separable Gaussian blur (ping-pong, N iterations)
//          → composite (scene + bloom*intensity, exposure tone-map) → default FBO

#include <glad/glad.h>
#include <data_mgr.hpp>
#include <memory>

namespace gld {

class BloomPipeline {
public:
    // Tunable parameters
    float threshold      = 0.85f;  // luminance where bloom starts
    float knee           = 0.4f;   // soft-knee width around threshold
    float intensity      = 1.15f;  // bloom add strength
    float exposure       = 1.0f;   // tone-map exposure
    int   blur_iterations = 5;     // full H+V blur passes

    void create(int w, int h)
    {
        width_ = w; height_ = h;

        bright_prog = DefDataMgr::instance()->load<DataType::Program>(
            "framebuffer/vert.glsl", "bloom/bright_fg.glsl");
        bright_prog->use();
        bright_prog->locat_uniforms("scene", "threshold", "knee");
        glUniform1i(bright_prog->uniform_id("scene"), 0);

        blur_prog = DefDataMgr::instance()->load<DataType::Program>(
            "framebuffer/vert.glsl", "bloom/blur_fg.glsl");
        blur_prog->use();
        blur_prog->locat_uniforms("image", "horizontal", "texel");
        glUniform1i(blur_prog->uniform_id("image"), 0);

        composite_prog = DefDataMgr::instance()->load<DataType::Program>(
            "framebuffer/vert.glsl", "bloom/composite_fg.glsl");
        composite_prog->use();
        composite_prog->locat_uniforms("scene", "bloomBlur", "intensity", "exposure");
        glUniform1i(composite_prog->uniform_id("scene"), 0);
        glUniform1i(composite_prog->uniform_id("bloomBlur"), 1);
        composite_prog->unuse();

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

    void begin()
    {
        glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
        glViewport(0, 0, width_, height_);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void end_and_present()
    {
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glViewport(0, 0, width_, height_);

        float texel[2] = { 1.f / (float)width_, 1.f / (float)height_ };

        // --- bright pass : scene_tex → blur_tex[0] ---------------------------
        glBindFramebuffer(GL_FRAMEBUFFER, blur_fbo_[0]);
        glClear(GL_COLOR_BUFFER_BIT);
        bright_prog->use();
        glUniform1f(bright_prog->uniform_id("threshold"), threshold);
        glUniform1f(bright_prog->uniform_id("knee"), knee);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, scene_tex_);
        draw_quad();

        // --- ping-pong Gaussian blur ----------------------------------------
        blur_prog->use();
        glUniform2fv(blur_prog->uniform_id("texel"), 1, texel);
        int  src = 0;                 // blur_tex[0] already holds bright output
        int  dst = 1;
        int  amount = blur_iterations * 2;
        for (int i = 0; i < amount; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, blur_fbo_[dst]);
            glClear(GL_COLOR_BUFFER_BIT);
            glUniform1i(blur_prog->uniform_id("horizontal"), (i % 2 == 0) ? 1 : 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, blur_tex_[src]);
            draw_quad();
            std::swap(src, dst);
        }
        int final_blur = src;         // last written target

        // --- composite to default framebuffer -------------------------------
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width_, height_);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        composite_prog->use();
        glUniform1f(composite_prog->uniform_id("intensity"), intensity);
        glUniform1f(composite_prog->uniform_id("exposure"), exposure);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, scene_tex_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, blur_tex_[final_blur]);
        draw_quad();

        composite_prog->unuse();
        glActiveTexture(GL_TEXTURE0);
    }

    ~BloomPipeline()
    {
        destroy_targets();
        if (quad_vao_) glDeleteVertexArrays(1, &quad_vao_);
        if (quad_vbo_) glDeleteBuffers(1, &quad_vbo_);
    }

private:
    void create_quad()
    {
        float quad[] = {
            // pos      // uv
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
        // Scene FBO: RGB16F colour + depth/stencil renderbuffer
        glGenFramebuffers(1, &scene_fbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
        scene_tex_ = make_color_tex();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene_tex_, 0);

        glGenRenderbuffers(1, &depth_rbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo_);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width_, height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_rbo_);

        // Ping-pong blur FBOs (colour only)
        glGenFramebuffers(2, blur_fbo_);
        for (int i = 0; i < 2; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, blur_fbo_[i]);
            blur_tex_[i] = make_color_tex();
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, blur_tex_[i], 0);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void destroy_targets()
    {
        if (scene_tex_) { glDeleteTextures(1, &scene_tex_); scene_tex_ = 0; }
        if (depth_rbo_) { glDeleteRenderbuffers(1, &depth_rbo_); depth_rbo_ = 0; }
        if (scene_fbo_) { glDeleteFramebuffers(1, &scene_fbo_); scene_fbo_ = 0; }
        for (int i = 0; i < 2; ++i) {
            if (blur_tex_[i]) { glDeleteTextures(1, &blur_tex_[i]); blur_tex_[i] = 0; }
            if (blur_fbo_[i]) { glDeleteFramebuffers(1, &blur_fbo_[i]); blur_fbo_[i] = 0; }
        }
    }

    int width_ = 0, height_ = 0;

    GLuint scene_fbo_ = 0, scene_tex_ = 0, depth_rbo_ = 0;
    GLuint blur_fbo_[2] = {0, 0};
    GLuint blur_tex_[2] = {0, 0};
    GLuint quad_vao_ = 0, quad_vbo_ = 0;

    std::shared_ptr<Program> bright_prog, blur_prog, composite_prog;
};

} // namespace gld
