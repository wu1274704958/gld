//
// Created by admin on 2020/2/20.
//

#ifndef GLD_EGLCXT_H
#define GLD_EGLCXT_H

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
#include <memory>
#include <android/native_window.h>
#include <atomic>
#include <mutex>
#include <android/log.h>

#define Loge(f,...) __android_log_print(ANDROID_LOG_ERROR,"EGLCxt @V@",f,##__VA_ARGS__)

struct RenderDemo;
struct EGLCxt;

typedef void (* WindowResizeFuncTy)(std::shared_ptr<EGLCxt>,int,int);
typedef void (* MouseButtonFunTy)(std::shared_ptr<EGLCxt>,int,int,int,int,int);
typedef void (* CursorPosFunTy)(std::shared_ptr<EGLCxt>,double,double);

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

struct android_app;

struct EGLCxt{
    GLint major = 0,minor = 0;
    EGLDisplay display  = nullptr;
    EGLConfig config = nullptr;
    EGLContext context = nullptr;
    EGLSurface surface = nullptr;
    EGLint format;
    android_app* app = nullptr;
    RenderDemo* renderDemo = nullptr;
    int width,height;


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
                EGL_DEPTH_SIZE, 24,
                EGL_ALPHA_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_RED_SIZE, 8,
                EGL_STENCIL_SIZE, 8,
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

        major   = oth.major;
        minor   = oth.minor;
        display = oth.display;
        config  = oth.config;
        context = oth.context;
        surface = oth.surface;
        format  = oth.format;
        width   = oth.width;
        height  = oth.height;
        app     = oth.app;
        renderDemo = oth.renderDemo;

        oth.clear();
    }

    EGLCxt& operator=(EGLCxt&& oth)
    {

        destroy();
        major   = oth.major;
        minor   = oth.minor;
        display = oth.display;
        config  = oth.config;
        context = oth.context;
        surface = oth.surface;
        format  = oth.format;
        width   = oth.width;
        height  = oth.height;
        app     = oth.app;
        renderDemo = oth.renderDemo;

        oth.clear();
        return *this;
    }

    void clear()
    {
        major = 0;
        minor = 0;
        display  = nullptr;
        config = nullptr;
        context = nullptr;
        surface = nullptr;
        app     = nullptr;
        width = height = 0;
        format = 0;
        renderDemo = nullptr;
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
        width = ANativeWindow_getWidth(window);
        height = ANativeWindow_getHeight(window);
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
            eglDestroySurface(display, surface);
            clear_current();
            surface = nullptr;
        }

    }

    void run_ui_thread(std::function<void(EGLCxt&)> f)
    {
        std::lock_guard guard(task_mutex);
        task_stack.push(f);
    }

    void run_task()
    {
        while(!task_stack.empty())
        {
            std::function<void(EGLCxt&)> f;
            {
                std::lock_guard guard(task_mutex);
                f = std::move(task_stack.top());
            }

            if(f) f(*this);

            {
                std::lock_guard guard(task_mutex);
                task_stack.pop();
            }
        }
    }

    ~EGLCxt()
    {
        if(display && context)
            eglDestroyContext(display, context);
        Loge("~EGLCxt()");
    }

    void set_window_size_callback(WindowResizeFuncTy ty)
    {
        windowResizeFunc = ty;
    }
    void set_mouse_button_callback(MouseButtonFunTy ty)
    {
        mouseButtonFun = ty;
    }
    void set_cursor_pos_callback(CursorPosFunTy ty)
    {
        cursorPosFun = ty;
    }

    bool is_runing()
    {
        return running;
    }

    void quit()
    {
        running = false;
    }

    void first_init(std::shared_ptr<EGLCxt> ptr);

    bool has_init()
    {
        return first_inited;
    }

    bool is_pause()
    {
        return m_pause;
    }

    void pause()
    {
        m_pause = true;
    }

    void resume()
    {
        m_pause = false;
    }

    bool has_surface()
    {
        return surface != nullptr;
    }

public:
    WindowResizeFuncTy windowResizeFunc = nullptr;
    MouseButtonFunTy mouseButtonFun = nullptr;
    CursorPosFunTy cursorPosFun = nullptr;
protected:
    std::atomic<bool> running = true;
    std::atomic<bool> first_inited = false;
    std::atomic<bool> m_pause = false;
    std::mutex task_mutex;
private:

    std::stack<std::function<void(EGLCxt&)>> task_stack;
};

#undef Loge

#endif //GLD_EGLCXT_H
