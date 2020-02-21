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
            cxt_p = std::make_shared<EGLCxt>(cxt);
        }catch (std::runtime_error e)
        {
            Loge("%s",e.what());
        }

        Loge("init success! %d.%d",cxt_p->major,cxt_p->minor);
    }
}

