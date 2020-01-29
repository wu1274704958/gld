#include <glad/glad.h>
#include <RenderDemo.h>

std::vector<RenderDemo*> RenderDemo::WindowResizeListeners = std::vector<RenderDemo*>();
std::vector<RenderDemo*> RenderDemo::MouseButtonListeners = std::vector<RenderDemo*>();
std::vector<RenderDemo*> RenderDemo::MouseMoveListeners = std::vector<RenderDemo*>();

int RenderDemo::initWindow(int w,int h,const char *title)
{
    if(!glfwInit())
    {
        error_c = -1;
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_window = glfwCreateWindow(w, h, title, NULL, NULL);
	if (!m_window)
	{
		glfwTerminate();
        error_c = -2;
		return -2;
	}

    glfwMakeContextCurrent(m_window);
	gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
	glfwSwapInterval(1);

    regOnWindowResizeListener(this);

    return 0;
}

int RenderDemo::init()
{
    glfwGetWindowSize(m_window, &width, &height);
    glfwSetWindowSizeCallback(m_window, WindowResizeCallBack);
    glfwSetMouseButtonCallback(m_window,MouseButtonCallBack);
    glfwSetCursorPosCallback(m_window,MouseMoveCallBack);
    return 0;
}

void RenderDemo::destroy()
{
    unregOnWindowResizeListener(this);
    if(error_c < 0)
        return;
    if(m_window )
        glfwDestroyWindow(m_window);
    glfwTerminate();
}

void RenderDemo::regOnWindowResizeListener(RenderDemo* rd)
{
    add_listener(WindowResizeListeners,rd);
}

void RenderDemo::unregOnWindowResizeListener(RenderDemo* rd)
{
    remove_listener(WindowResizeListeners,rd);
}

void RenderDemo::WindowResizeCallBack(GLFWwindow* window, int w, int h)
{
    call_listeners(WindowResizeListeners,window,[w,h](RenderDemo* it){
        it->width = w;
        it->height = h;
        it->onWindowResize(w, h);
    });
}

void RenderDemo::regOnMouseButtonListener(RenderDemo* rd)
{
    add_listener(MouseButtonListeners,rd);
}

void RenderDemo::unregOnMouseButtonListener(RenderDemo* rd)
{
    remove_listener(MouseButtonListeners,rd);
}

void RenderDemo::MouseButtonCallBack(GLFWwindow* window,int btn,int action,int mod)
{
    call_listeners<int,int,int>(MouseButtonListeners,window,&RenderDemo::onMouseButton,btn,action,mod);
}


void RenderDemo::regOnMouseMoveListener(RenderDemo* rd)
{
    add_listener(MouseMoveListeners,rd);
}
void RenderDemo::unregOnMouseMoveListener(RenderDemo* rd)
{
    remove_listener(MouseMoveListeners,rd);
}
void RenderDemo::MouseMoveCallBack(GLFWwindow* window,double x,double y)
{
    call_listeners<double,double>(MouseMoveListeners,window,&RenderDemo::onMouseMove,x,y);
}

void RenderDemo::run()
{
    while (!glfwWindowShouldClose(m_window))
	{
        draw();
        glfwSwapBuffers(m_window);
		glfwPollEvents();
	}
}

void RenderDemo::onWindowResize(int w, int h)
{
}

void RenderDemo::onMouseButton(int,int,int){}
void RenderDemo::onMouseMove(double,double){}

RenderDemo::~RenderDemo()
{
    destroy();
}

void RenderDemo::remove_listener(std::vector<RenderDemo*>& list,RenderDemo* rd)
{
    if (auto it = std::find(list.begin(), list.end(), rd);it != list.end())
    {
        list.erase(it);
    }
}
void RenderDemo::add_listener(std::vector<RenderDemo*>& list,RenderDemo* rd)
{
    if (std::find(list.begin(), list.end(), rd) == list.end())
    {
        list.push_back(rd);
    }
}

void RenderDemo::call_listeners(std::vector<RenderDemo*>& list,GLFWwindow* window,std::function<void(RenderDemo*)> func)
{
    for (auto it : list)
    {
        if(it->m_window == window)
        {
            func(it);
        }
    }
}

