
#ifndef PF_ANDROID
#include <glad/glad.h>
#define Loge(...)
#else
#include <EGLCxt.h>
#include <android/log.h>
#define Loge(f,...) __android_log_print(ANDROID_LOG_ERROR,"RenderDemo @V@",f,##__VA_ARGS__)
#endif

#include <RenderDemo.h>

std::vector<RenderDemo*> RenderDemo::WindowResizeListeners = std::vector<RenderDemo*>();
std::vector<RenderDemo*> RenderDemo::MouseButtonListeners = std::vector<RenderDemo*>();
std::vector<RenderDemo*> RenderDemo::MouseMoveListeners = std::vector<RenderDemo*>();

#ifndef PF_ANDROID
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
#else
    void  RenderDemo::set_egl_cxt(int w,int h,std::shared_ptr<EGLCxt> cxt)
    {
        this->m_window = std::move(cxt);
        width = w;
        height = h;

        ResMgrWithGlslPreProcess::create_instance(m_window);
        DefResMgr::create_instance(m_window);
    }
#endif

int RenderDemo::init()
{
#ifndef PF_ANDROID
    glfwGetWindowSize(m_window, &width, &height);
    glfwSetWindowSizeCallback(m_window, WindowResizeCallBack);
    glfwSetMouseButtonCallback(m_window,MouseButtonCallBack);
    glfwSetCursorPosCallback(m_window,MouseMoveCallBack);
#else
    m_window->set_window_size_callback(WindowResizeCallBack);
    m_window->set_mouse_button_callback(MouseButtonCallBack);
    m_window->set_cursor_pos_callback(MouseMoveCallBack);
#endif
    return 0;
}

void RenderDemo::destroy()
{
    unregOnWindowResizeListener(this);
    unregOnMouseMoveListener(this);
    unregOnMouseButtonListener(this);
    if(error_c < 0)
        return;
#ifndef PF_ANDROID
    if(m_window )
        glfwDestroyWindow(m_window);
    glfwTerminate();
#endif
}

void RenderDemo::regOnWindowResizeListener(RenderDemo* rd)
{
    add_listener(WindowResizeListeners,rd);
}

void RenderDemo::unregOnWindowResizeListener(RenderDemo* rd)
{
    remove_listener(WindowResizeListeners,rd);
}

void RenderDemo::WindowResizeCallBack(RenderDemo::WINDOW_TYPE window, int w, int h)
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

void RenderDemo::MouseButtonCallBack(RenderDemo::WINDOW_TYPE window,int btn,int action,int mod)
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
void RenderDemo::MouseMoveCallBack(RenderDemo::WINDOW_TYPE window,double x,double y)
{
    call_listeners<double,double>(MouseMoveListeners,window,&RenderDemo::onMouseMove,x,y);
}

void RenderDemo::run()
{
#ifndef PF_ANDROID
    while (!glfwWindowShouldClose(m_window))
	{
        draw();
        glfwSwapBuffers(m_window);
		glfwPollEvents();
	}
#endif
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

void RenderDemo::call_listeners(std::vector<RenderDemo*>& list,RenderDemo::WINDOW_TYPE window,std::function<void(RenderDemo*)> func)
{
    for (auto it : list)
    {
#ifndef PF_ANDROID
        if(it->m_window == window)
#endif
        {
            func(it);
        }
    }
}

