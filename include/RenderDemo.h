#include <GLFW/glfw3.h>
#include <vector>

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
protected:
    virtual void destroy();
    virtual void draw() = 0;
    GLFWwindow *m_window;
    int width, height;

    static std::vector<RenderDemo*> WindowResizeListeners;

    static void regOnWindowResizeListener(RenderDemo* rd);

    static void unregOnWindowResizeListener(RenderDemo* rd);

    static void onWindowResize(GLFWwindow* window, int w, int h);

private:
     int error_c = 0;
};