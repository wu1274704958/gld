#include <glad/glad.h>
#include <RenderDemo.h>


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
    return 0;
}

void RenderDemo::destroy()
{
    if(error_c < 0)
        return;
    if(m_window )
        glfwDestroyWindow(m_window);
    glfwTerminate();
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

RenderDemo::~RenderDemo()
{
    destroy();
}

