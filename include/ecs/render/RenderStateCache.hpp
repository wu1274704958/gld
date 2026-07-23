#pragma once

#include <glad/glad.h>

#include "RenderComponents.hpp"

namespace gld::ecs {

    struct RenderStateCache{
        unsigned int framebuffer = static_cast<unsigned int>(-1);
        int viewport_w = -1;
        int viewport_h = -1;
        bool blend_known = false;
        bool blend_enabled = false;
        bool depth_known = false;
        bool depth_enabled = false;
        bool depth_write_known = false;
        bool depth_write_enabled = false;
        bool blend_func_known = false;
        BlendFactor blend_src = BlendFactor::SrcAlpha;
        BlendFactor blend_dst = BlendFactor::OneMinusSrcAlpha;
        BlendEquation blend_equation = BlendEquation::Add;
        bool stencil_known = false;
        bool stencil_enabled = false;
        bool stencil_func_known = false;
        StencilFaceState stencil_state;

        void bind_framebuffer(unsigned int target) {
            if (framebuffer == target) return;
            glBindFramebuffer(GL_FRAMEBUFFER, target);
            framebuffer = target;
        }

        void viewport(int width, int height) {
            if (viewport_w == width && viewport_h == height) return;
            glViewport(0, 0, width, height);
            viewport_w = width;
            viewport_h = height;
        }

        void blend(bool enabled) {
            if (blend_known && blend_enabled == enabled) return;
            if (enabled) glEnable(GL_BLEND);
            else glDisable(GL_BLEND);
            blend_known = true;
            blend_enabled = enabled;
        }

        void depth(bool enabled) {
            if (depth_known && depth_enabled == enabled) return;
            if (enabled) glEnable(GL_DEPTH_TEST);
            else glDisable(GL_DEPTH_TEST);
            depth_known = true;
            depth_enabled = enabled;
        }

        void depth_write(bool enabled) {
            if (depth_write_known && depth_write_enabled == enabled) return;
            glDepthMask(enabled ? GL_TRUE : GL_FALSE);
            depth_write_known = true;
            depth_write_enabled = enabled;
        }

        void blend_func(BlendFactor src, BlendFactor dst, BlendEquation equation) {
            if (blend_func_known && blend_src == src && blend_dst == dst && blend_equation == equation) return;
            glBlendFunc(static_cast<GLenum>(src), static_cast<GLenum>(dst));
            glBlendEquation(static_cast<GLenum>(equation));
            blend_func_known = true;
            blend_src = src;
            blend_dst = dst;
            blend_equation = equation;
        }

        void stencil(bool enabled) {
            if (stencil_known && stencil_enabled == enabled) return;
            if (enabled) glEnable(GL_STENCIL_TEST);
            else glDisable(GL_STENCIL_TEST);
            stencil_known = true;
            stencil_enabled = enabled;
        }

        void stencil_func_ops(const StencilFaceState& state) {
            if (stencil_func_known &&
                stencil_state.func == state.func &&
                stencil_state.ref == state.ref &&
                stencil_state.mask == state.mask &&
                stencil_state.fail == state.fail &&
                stencil_state.depth_fail == state.depth_fail &&
                stencil_state.pass == state.pass) {
                return;
            }

            glStencilFunc(static_cast<GLenum>(state.func), state.ref, state.mask);
            glStencilOp(static_cast<GLenum>(state.fail),
                        static_cast<GLenum>(state.depth_fail),
                        static_cast<GLenum>(state.pass));
            stencil_func_known = true;
            stencil_state = state;
        }

        void apply(const ResolvedRenderPassState& state) {
            blend(state.blend);
            if (state.blend)
                blend_func(state.blend_src, state.blend_dst, state.blend_equation);
            depth(state.depth_test);
            depth_write(state.depth_write);
            stencil(state.stencil);
            if (state.stencil)
                stencil_func_ops(state.stencil_state);
        }
    };
}
