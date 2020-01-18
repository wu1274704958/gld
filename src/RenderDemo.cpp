#include <glad/glad.h>
#include <RenderDemo.h>

std::vector<RenderDemo*> RenderDemo::WindowResizeListeners = std::vector<RenderDemo*>();

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
    glfwSetWindowSizeCallback(m_window, onWindowResize);
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
    if (std::find(WindowResizeListeners.begin(), WindowResizeListeners.end(), rd) == WindowResizeListeners.end())
    {
        WindowResizeListeners.push_back(rd);
    }
}

void RenderDemo::unregOnWindowResizeListener(RenderDemo* rd)
{
    if (auto it = std::find(WindowResizeListeners.begin(), WindowResizeListeners.end(), rd);it != WindowResizeListeners.end())
    {
        WindowResizeListeners.erase(it);
    }
}

void RenderDemo::onWindowResize(GLFWwindow* window, int w, int h)
{
    for (auto it : WindowResizeListeners)
    {
        it->width = w;
        it->height = h;
        it->onWindowResize(w, h);
    }
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

RenderDemo::~RenderDemo()
{
    destroy();
}

