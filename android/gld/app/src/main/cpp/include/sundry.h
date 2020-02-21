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
#include <functional>
#include <stack>

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
    EGLDisplay display  = nullptr;
    EGLConfig config = nullptr;
    EGLContext context = nullptr;
    EGLSurface surface = nullptr;
    EGLint format;

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

        context = eglCreateContext(display, config, EGL_NO_CONTEXT, attributes);
        if (context == EGL_NO_CONTEXT)
        {
            throw std::runtime_error(link_str("eglCreateContext returned err ", eglGetError()));
        }


        if (!eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format)) {
            throw std::runtime_error(link_str("eglGetConfigAttrib returned err ", eglGetError()));
        }

    }

    EGLCxt(EGLCxt&& oth)
    {
        destroy();

        major   = oth.major;
        minor   = oth.minor;
        display = oth.display;
        config  = oth.config;
        context = oth.context;
        surface = oth.surface;
        format  = oth.format;

        oth.clear();
    }

    EGLCxt& operator=(EGLCxt&& oth)
    {

        major   = oth.major;
        minor   = oth.minor;
        display = oth.display;
        config  = oth.config;
        context = oth.context;
        surface = oth.surface;
        format  = oth.format;

        oth.clear();
    }

    void clear()
    {
        major = 0;
        minor = 0;
        display  = nullptr;
        config = nullptr;
        context = nullptr;
        surface = nullptr;
        format = 0;
    }

    void destroy()
    {
        destroy_surface();
        if(display && context)
            eglDestroyContext(display, context);
    }

    EGLCxt(const EGLCxt&) = delete;

    void create_surface(EGLNativeWindowType window)
    {
        // 创建 On-Screen 渲染区域
        surface = eglCreateWindowSurface(display, config, window, 0);
        if (surface == EGL_NO_SURFACE) {
            throw std::runtime_error(link_str("eglCreateWindowSurface failed: ", eglGetError()));
        }
    }

    void make_current()
    {
        if (!eglMakeCurrent(display, surface, surface, context)) {
            throw std::runtime_error(link_str("eglMakeCurrent failed: ", eglGetError()));
        }
    }

    void clear_current()
    {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    void destroy_surface()
    {
        if(surface)
        {
            clear_current();
            eglDestroySurface(display, surface);
        }

    }

    void run_ui_thread(std::function<void(EGLCxt&)> f)
    {
        task_stack.push(f);
    }

    void run_task()
    {
        while(!task_stack.empty())
        {
            auto f = std::move(task_stack.top());
            if(f) f(*this);
            task_stack.pop();
        }
    }

    ~EGLCxt()
    {
        destroy();
    }

private:
    std::stack<std::function<void(EGLCxt&)>> task_stack;
};



#endif //GLD_SUNDRY_H
