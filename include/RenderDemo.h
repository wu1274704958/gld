#include <GLFW/glfw3.h>
#include <vector>
#include <functional>

class RenderDemo{
public: 
    RenderDemo() = default; 
    RenderDemo(RenderDemo&) = delete;
    RenderDemo(RenderDemo&&) = delete;
    virtual ~RenderDemo();
    int initWindow(int w,int h,const char *title);
    virtual int init();
    virtual void run();
    virtual void onWindowResize(int w,int h);
    virtual void onMouseButton(int,int,int);
    virtual void onMouseMove(double,double);
    
protected:
    virtual void destroy();
    virtual void draw() = 0;
    GLFWwindow *m_window;
    int width, height;
//-----------------------------------------------------------
    static std::vector<RenderDemo*> WindowResizeListeners;

    static void regOnWindowResizeListener(RenderDemo* rd);

    static void unregOnWindowResizeListener(RenderDemo* rd);

    static void WindowResizeCallBack(GLFWwindow* window, int w, int h);
//-----------------------------------------------------------
    static std::vector<RenderDemo*> MouseButtonListeners;

    static void regOnMouseButtonListener(RenderDemo* rd);

    static void unregOnMouseButtonListener(RenderDemo* rd);

    static void MouseButtonCallBack(GLFWwindow*,int,int,int);
//-----------------------------------------------------------
    static std::vector<RenderDemo*> MouseMoveListeners;

    static void regOnMouseMoveListener(RenderDemo* rd);

    static void unregOnMouseMoveListener(RenderDemo* rd);

    static void MouseMoveCallBack(GLFWwindow*,double,double);
    
    static void remove_listener(std::vector<RenderDemo*>& list,RenderDemo* rd);
    static void add_listener(std::vector<RenderDemo*>& list,RenderDemo* rd);

    template<typename ...Args>
    static void call_listeners(std::vector<RenderDemo*>& list,GLFWwindow* window,void(RenderDemo::*func)(Args...),Args ...args)
    {
        for (auto it : list)
        {
            if(it->m_window == window)
            {
                ((*it).*func)(std::forward<Args>(args)...);
            }
        }
    }

    static void call_listeners(std::vector<RenderDemo*>& list,GLFWwindow* window,std::function<void(RenderDemo*)> func);

private:
     int error_c = 0;
};