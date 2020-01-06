#include <GLFW/glfw3.h>

class RenderDemo{
public: 
    RenderDemo() = default; 
    RenderDemo(RenderDemo&) = delete;
    RenderDemo(RenderDemo&&) = delete;
    virtual ~RenderDemo();
    int initWindow(int w,int h,const char *title);
    virtual int init() = 0;
    virtual void run();
protected:
    virtual void destroy();
    virtual void draw() = 0;
    GLFWwindow *m_window;
private:
     int error_c = 0;
};