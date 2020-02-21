#include <native_app_glue/android_native_app_glue.h>
#include <android/log.h>
#include <sundry.h>
#include <pthread.h>
#include <memory>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>


#define Loge(f,...) __android_log_print(ANDROID_LOG_ERROR,"NativeActivity @V@",f,##__VA_ARGS__)

static void ProcessAndroidCmd(struct android_app* app, int32_t cmd);

extern "C" {
    void android_main(struct android_app* app)
    {
#ifdef PF_ANDROID
        Loge("main() %d",1);
#else
        Loge("main() %d",0);
#endif
        std::shared_ptr<EGLCxt> cxt_p;
        try{
            EGLCxt cxt;
            cxt_p = std::make_shared<EGLCxt>(std::move(cxt));
        }catch (std::runtime_error e)
        {
            Loge("%s",e.what());
        }
        Loge("init success! %d.%d",cxt_p->major,cxt_p->minor);
        app->userData = &cxt_p;
        //（1）指定cmd处理方法
        app->onAppCmd = ProcessAndroidCmd;

        // loop waiting for stuff to do.
        while (true) {
            // Read all pending events.
            int events;
            struct android_poll_source *source;

            while (ALooper_pollAll(0, NULL, &events, (void **) &source) >= 0) {
                // Process this event.
                if (source != NULL) {
                    source->process(app, source);
                }

                // Check if we are exiting.
                if (app->destroyRequested != 0) {
                    Loge("destroy requested!");
                    return;
                }
            }

            cxt_p->run_task();

            //（3）TODO：画帧
        }
    }

    static void ProcessAndroidCmd(struct android_app* app, int32_t cmd) {
        std::shared_ptr<EGLCxt> cxt_p = *reinterpret_cast<std::shared_ptr<EGLCxt>*>(app->userData);
        try {
            switch (cmd) {
                case APP_CMD_INIT_WINDOW:
                    cxt_p->run_ui_thread([app](EGLCxt& cxt){
                        cxt.create_surface(app->window);
                        cxt.make_current();
                        Loge("Create surface success!!");
                    });
                    break;
                case APP_CMD_TERM_WINDOW:
                    cxt_p->destroy_surface();
                    break;
                case APP_CMD_CONFIG_CHANGED:

                    break;
                case APP_CMD_LOST_FOCUS:

                    break;
            }
        }catch (std::runtime_error e)
        {
            Loge("ProcessAndroidCmd: \n%s",e.what());
        }
    }

}

