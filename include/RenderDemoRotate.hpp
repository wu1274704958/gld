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
            rotate_factor = glm::vec2(0.1f,0.1f);
            regOnMouseButtonListener(this);
            regOnMouseMoveListener(this);
            return 0;
        }
        void onMouseButton(int btn,int action,int mode) override
        {
            if(btn == GLFW_MOUSE_BUTTON_1)
                mouse_key_left_press = action == GLFW_PRESS;
        }
    

        void onMouseMove(double x,double y) override
        {
            if(mouse_key_left_press)
            {
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
            last_mouse_pos.x = static_cast<float>(x);
            last_mouse_pos.y = static_cast<float>(y);
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



