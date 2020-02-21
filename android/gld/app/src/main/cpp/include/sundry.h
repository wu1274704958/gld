//
// Created by admin on 2020/2/20.
//

#ifndef GLD_SUNDRY_H
#define GLD_SUNDRY_H

#include <tuple>
#include <optional>
#include <cstdarg>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <string>
#include <EGL/eglext.h>
#include <serialization.hpp>

template <typename F,typename ...Args>
std::string link_str_ex(std::string &str,F &&f,Args&& ...args){
    str += wws::to_string(std::forward<F>(f));
    if constexpr (sizeof...(Args) > 0)
    {
        return link_str_ex(str,std::forward<Args>(args)...);
    }else
    {
        return str;
    }
}

template <typename F,typename ...Args>
std::string link_str(F &&f,Args&& ...args){
    std::string str(std::forward<F>(f));
    if constexpr (sizeof...(Args) > 0)
    {
        return link_str_ex(str,std::forward<Args>(args)...);
    }else
    {
        return str;
    }
}

struct EGLCxt{
    GLint major = 0,minor = 0;
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    EGLSurface surface = nullptr;
    EGLint format;
    bool maked_current = false;

    EGLCxt(bool es3 = false) {

        display = EGL_NO_DISPLAY;

        if ((display = eglGetDisplay(EGL_DEFAULT_DISPLAY)) == EGL_NO_DISPLAY) {
            throw std::runtime_error(link_str("eglGetDisplay returned err ", eglGetError()));
        }

        if (!eglInitialize(display, &major, &minor)) {
            throw std::runtime_error(link_str("eglInitialize returned err ", eglGetError()));
        }
        EGLint attribs[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                EGL_DEPTH_SIZE, 16,
                EGL_ALPHA_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_RED_SIZE, 8,
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_NONE};
        if(es3)
            attribs[1] = EGL_OPENGL_ES3_BIT_KHR;

        EGLint config_len;
        if (!eglChooseConfig(display, attribs, &config, 1, &config_len)) {
            throw std::runtime_error(link_str("eglChooseConfig returned err ", eglGetError()));
        }

        EGLint attributes[] = {EGL_CONTEXT_CLIENT_VERSION, es3 ? 3 : 2, EGL_NONE};

        if (!(context = eglCreateContext(display, config, NULL, attributes))) {
            throw std::runtime_error(link_str("eglCreateContext returned err ", eglGetError()));
        }

        if (!eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format)) {
            throw std::runtime_error(link_str("eglGetConfigAttrib returned err ", eglGetError()));
        }

    }

    void create_surface(EGLNativeWindowType window)
    {
        // 创建 On-Screen 渲染区域
        surface = eglCreateWindowSurface(display, config, window, 0);
        if (surface == EGL_NO_SURFACE) {
            throw std::runtime_error(link_str("eglCreateWindowSurface failed: %d", eglGetError()));
        }
    }

    void make_current()
    {
        if (!eglMakeCurrent(display, surface, surface, context)) {
            throw std::runtime_error(link_str("eglMakeCurrent failed: %d", eglGetError()));
        }
        maked_current = true;
    }

    ~EGLCxt()
    {
        if(surface)
            eglDestroySurface(display, surface);
        if(maked_current)
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display, context);
        maked_current = false;
    }
};



#endif //GLD_SUNDRY_H
