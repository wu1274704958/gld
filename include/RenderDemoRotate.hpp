#include <RenderDemo.h>
#include <glm/glm.hpp>

namespace gld{
    class RenderDemoRotate : public RenderDemo
    {
    public:
        int init() override
        {
            RenderDemo::init();
            rotate = glm::vec3(0.f,0.f,0.f);

            calc_rotate_factor();
            regOnMouseButtonListener(this);
            regOnMouseMoveListener(this);
            return 0;
        }
        void onMouseButton(int btn,int action,int mode) override
        {
            if(btn == GLFW_MOUSE_BUTTON_1)
            {
                if(action == GLFW_PRESS)
                {
                    mouse_key_left_press = true;
                }
                else if(action == GLFW_RELEASE)
                {
                    mouse_key_left_press = false;
                    last_mouse_pos = glm::vec2(-1.0f,-1.0f);
                }
            }
        }

        void onWindowResize(int w,int h) override
        {
            calc_rotate_factor();
        }

        virtual void calc_rotate_factor()
        {
#ifndef PF_ANDROID
            rotate_factor = glm::vec2(width / 30000.f,height / 30000.f);
#else
            rotate_factor = glm::vec2(width / 900000.f,height / 900000.f);
#endif
        }
    

        void onMouseMove(double x,double y) override
        {
            if(mouse_key_left_press)
            {
                if(last_mouse_pos.x == -1 && last_mouse_pos.y == -1)
                {
                    last_mouse_pos.x = static_cast<float>(x);
                    last_mouse_pos.y = static_cast<float>(y);
                }
                rotate.y += rotate_factor.x * (static_cast<float>(x) - last_mouse_pos.x);
                rotate.x += rotate_factor.y * (static_cast<float>(y) - last_mouse_pos.y);
                if(rotate.x > 360.f)
                    rotate.x -= 360.f;
                if(rotate.x < 0)
                    rotate.x += 360.f;
                if(rotate.y > 360.f)
                    rotate.y -= 360.f;    
                if(rotate.y < 0)
                    rotate.y += 360.f;
            }
#ifndef PF_ANDROID
            last_mouse_pos.x = static_cast<float>(x);
            last_mouse_pos.y = static_cast<float>(y);
#endif
        }

        glm::vec2& get_rotate_factor()
        {
            return rotate_factor;
        }
    protected:
        bool mouse_key_left_press = false;
        glm::vec3 rotate;
        glm::vec2 last_mouse_pos,rotate_factor;
    };
}



