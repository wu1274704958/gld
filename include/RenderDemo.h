#pragma once
#ifdef PF_ANDROID
#include <GlfwDef.h>
#else
#include <GLFW/glfw3.h>
#endif

#include <memory>
#include <vector>
#include <functional>

struct EGLCxt;

class RenderDemo{
public: 

#ifdef PF_ANDROID
    using WINDOW_TYPE = std::shared_ptr<EGLCxt>;
#else
protected:
    using WINDOW_TYPE = GLFWwindow *;
#endif

    RenderDemo() = default; 
    RenderDemo(RenderDemo&) = delete;
    RenderDemo(RenderDemo&&) = delete;
    virtual ~RenderDemo();
#ifndef PF_ANDROID
    int initWindow(int w,int h,const char *title);
    int initWindow(int w,int h,const char *title,std::function<void()> preCreate);
#else
    void set_egl_cxt(int w,int h,std::shared_ptr<EGLCxt> cxt);
#endif
    virtual int init();
    virtual void run();
    virtual void onWindowResize(int w,int h);
    virtual void onMouseButton(int,int,int,int,int);
    virtual void onMouseMove(double,double);

    WINDOW_TYPE get_window();

#ifdef PF_ANDROID
public:
#else
protected:
#endif
    virtual void destroy();
    virtual void draw() = 0;

    WINDOW_TYPE m_window;

    int width, height;
//-----------------------------------------------------------
    static std::vector<RenderDemo*> WindowResizeListeners;

    static void regOnWindowResizeListener(RenderDemo* rd);

    static void unregOnWindowResizeListener(RenderDemo* rd);

    static void WindowResizeCallBack(WINDOW_TYPE window, int w, int h);
//-----------------------------------------------------------
    static std::vector<RenderDemo*> MouseButtonListeners;

    static void regOnMouseButtonListener(RenderDemo* rd);

    static void unregOnMouseButtonListener(RenderDemo* rd);

    static void MouseButtonCallBack(WINDOW_TYPE,int,int,int);
    static void MouseButtonCallBackEx(WINDOW_TYPE, int, int, int, int, int);
//-----------------------------------------------------------
    static std::vector<RenderDemo*> MouseMoveListeners;

    static void regOnMouseMoveListener(RenderDemo* rd);

    static void unregOnMouseMoveListener(RenderDemo* rd);

    static void MouseMoveCallBack(WINDOW_TYPE,double,double);
    
    static void remove_listener(std::vector<RenderDemo*>& list,RenderDemo* rd);
    static void add_listener(std::vector<RenderDemo*>& list,RenderDemo* rd);

    template<typename ...Args>
    static void call_listeners(std::vector<RenderDemo*>& list,WINDOW_TYPE window,void(RenderDemo::*func)(Args...),Args ...args)
    {
        for (auto it : list)
        {
#ifndef PF_ANDROID
            if(it->m_window == window)
#endif
            {
                ((*it).*func)(std::forward<Args>(args)...);
            }
        }
    }

    static void call_listeners(std::vector<RenderDemo*>& list,WINDOW_TYPE window,std::function<void(RenderDemo*)> func);

private:
     int error_c = 0;
};