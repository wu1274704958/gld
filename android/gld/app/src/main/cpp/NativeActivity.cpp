#include <native_app_glue/android_native_app_glue.h>
#include <android/log.h>
#include <EGLCxt.h>
#include <pthread.h>
#include <memory>

#include <MapEvent.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <RenderDemo.h>
//#include <d1/main.cpp>
//#include <lighting_2/main.cpp>
//#include <stencilt_test/main.cpp>
#include <example/framebuffer/main.cpp>
#include <data_mgr.hpp>
#include <frame_rate.h>
using namespace gld;

#define Loge(f,...) __android_log_print(ANDROID_LOG_ERROR,"NativeActivity @V@",f,##__VA_ARGS__)

static void ProcessAndroidCmd(struct android_app* app, int32_t cmd);
static int32_t onEvent(struct android_app* app, AInputEvent* event);

extern "C" {
    void android_main(struct android_app* app)
    {
#ifdef PF_ANDROID
        Loge("main() %d",1);
#else
        Loge("main() %d",0);
#endif
        Demo1 d1;

        std::shared_ptr<EGLCxt> cxt_p;
        try{
            EGLCxt cxt(true);
            cxt_p = std::make_shared<EGLCxt>(std::move(cxt));
            cxt_p->app = app;
            cxt_p->renderDemo = dynamic_cast<RenderDemo*>(&d1);

        }catch (std::runtime_error& e)
        {
            Loge("%s",e.what());
        }

        Loge("init success! %d.%d",cxt_p->major,cxt_p->minor);
        app->userData = &cxt_p;
        //（1）指定cmd处理方法
        app->onAppCmd = ProcessAndroidCmd;
        app->onInputEvent = onEvent;

        // loop waiting for stuff to do.
        while (cxt_p->is_runing()) {
            // Read all pending events.
            auto calc = gld::FrameRate::calculator();
            int events;
            struct android_poll_source *source;

            while (ALooper_pollAll(0, nullptr, &events, (void **) &source) >= 0) {
                // Process this event.
                if (source != nullptr) {
                    source->process(app, source);
                }
                // Check if we are exiting.
                if (app->destroyRequested != 0) {
                    Loge("destroy requested!");
                    cxt_p->quit();
                }
            }

            cxt_p->run_task();

            //（3）TODO：画帧
            if(cxt_p->has_init() && !cxt_p->is_pause() && cxt_p->has_surface())
            {
                cxt_p->renderDemo->draw();
                eglSwapBuffers(cxt_p->display, cxt_p->surface);
            }
        }
        cxt_p->destroy_surface();
        Loge("exit!!!");
        DefDataMgr::instance()->clear_all();
    }

    static void ProcessAndroidCmd(struct android_app* app, int32_t cmd) {
        std::shared_ptr<EGLCxt> cxt_p = *reinterpret_cast<std::shared_ptr<EGLCxt>*>(app->userData);
        try {
            switch (cmd) {
                case APP_CMD_INIT_WINDOW:
                    cxt_p->run_ui_thread([app,cxt_p](EGLCxt& cxt){
                        cxt.create_surface(app->window);
                        cxt.make_current();
                        if(cxt.has_init())
                        {
                            if(cxt.windowResizeFunc)
                                cxt.windowResizeFunc(std::move(cxt_p), cxt.width, cxt.height);
                        }else
                            cxt.first_init(std::move(cxt_p));
                        Loge("Create surface success!! %d %d",cxt.width,cxt.height);
                    });
                    break;
                case APP_CMD_TERM_WINDOW:
                    cxt_p->destroy_surface();
                    break;
                case APP_CMD_CONFIG_CHANGED:

                    break;
                case APP_CMD_LOST_FOCUS:

                    break;
                case APP_CMD_DESTROY:
                    cxt_p->quit();
                    break;
                case APP_CMD_PAUSE:
                    cxt_p->pause();
                    break;
                case APP_CMD_RESUME:
                    cxt_p->resume();
                    break;
            }
        }catch (std::runtime_error& e)
        {
            Loge("ProcessAndroidCmd: \n%s",e.what());
        }
    }

    static int32_t onEvent(struct android_app* app, AInputEvent* event)
    {
        std::shared_ptr<EGLCxt> cxt_p = *reinterpret_cast<std::shared_ptr<EGLCxt>*>(app->userData);
        if(!EventMap::is_init())
            EventMap::init_runtime_map();
        auto ty = AInputEvent_getType(event);
        switch (ty)
        {
            case AINPUT_EVENT_TYPE_MOTION:
                Loge("motion event %zu \n %f  %f",EventMap::map_ex(
                        static_cast<size_t>(AMotionEvent_getAction(event))),AMotionEvent_getRawX(event,0),AMotionEvent_getRawY(event,0));
                if(cxt_p->mouseButtonFun)
                    cxt_p->mouseButtonFun(cxt_p, GLFW_MOUSE_BUTTON_1,
                     static_cast<int>(EventMap::map_ex(static_cast<size_t>(AMotionEvent_getAction(event)))), 0);
                if(cxt_p->cursorPosFun)
                    cxt_p->cursorPosFun(cxt_p,AMotionEvent_getRawX(event,0),AMotionEvent_getRawY(event,0));
                break;
        }
        return 1;
    }

}


void EGLCxt::first_init(std::shared_ptr<EGLCxt> ptr)
{
    renderDemo->set_egl_cxt(width,height,std::move(ptr));
    renderDemo->init();
    first_inited = true;
    Loge("first inited!!!");
}

