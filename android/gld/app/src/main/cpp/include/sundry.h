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

struct EGLCxt{
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    EGLSurface surface;
    EGLint format;
};

template <typename F,typename ...Args>
std::string link_str_ex(std::string &str,F &&f,Args&& ...args){
    str += std::forward<F>(f);
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


std::optional<EGLCxt> initOpenGlES() {

    EGLDisplay display = EGL_NO_DISPLAY;

    if ((display = eglGetDisplay(EGL_DEFAULT_DISPLAY)) == EGL_NO_DISPLAY) {
        throw std::runtime_error(link_str("eglGetDisplay returned err ", eglGetError()));
    }
    GLint major = 3,minor = 3;

    if (!eglInitialize(display, &major, &minor)) {
        throw std::runtime_error(link_str("eglInitialize returned err ", eglGetError()));
    }
    EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
            EGL_BUFFER_SIZE, 32,
            EGL_ALPHA_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_NONE};

    EGLConfig  config;
    EGLint config_len;
    if (!eglChooseConfig(display, attribs, &config, 1, &config_len)) {
        throw std::runtime_error(link_str("eglChooseConfig returned err ", eglGetError()));
    }
    EGLint attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    EGLContext context;
    if (!(context = eglCreateContext(display, config, NULL, attributes))) {
        throw std::runtime_error(link_str("eglCreateContext returned err ", eglGetError()));
    }
    EGLSurface surface = NULL;
    EGLint format;
    if (!eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format)) {
        throw std::runtime_error(link_str("eglGetConfigAttrib returned err ", eglGetError()));
    }
    return std::make_optional<EGLCxt>({display,config,context,surface,format});
}

#endif //GLD_SUNDRY_H
