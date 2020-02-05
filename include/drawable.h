#pragma once

#include "program.hpp"
#include "vertex_arr.hpp"
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <uniform.hpp>

namespace gld{

    class Drawable{
    public:
        
        Drawable(Program& program,std::string model_key) :
            program(program),
            model(std::move(model_key),program)
        {
            rotate = pos = glm::vec3(0.f, 0.f, 0.f);
            scale = glm::vec3(1.f, 1.f, 1.f);
        }

        virtual void init()
        {
            onInit();
        }

        virtual void draw()
        {
            if (visible)
            {
                preDraw();
                onDraw();
            }
        }

        virtual void preDraw()
        {
            glm::mat4 model(1.0f);

            model = glm::translate(model, pos);

            model = glm::scale(model, scale);

            model = glm::rotate(model, rotate.x, glm::vec3(1.f, 0.f, 0.f));
            model = glm::rotate(model, rotate.y, glm::vec3(0.f, 1.f, 0.f));
            model = glm::rotate(model, rotate.z, glm::vec3(0.f, 0.f, 1.f));

            update_matrix(model);
            onPreDraw();
        }

        virtual void update_matrix(glm::mat4 mat)
        {
            program.use();
            model = glm::value_ptr(mat);
        }

        void setRotateX(float x) {
            rotate.x = glm::radians(x);
        }
        void setRotateY(float x) {
            rotate.y = glm::radians(x);
        }
        void setRotateZ(float x) {
            rotate.z = glm::radians(x);
        }
        void setRotateZ(glm::vec3 r) {
            rotate.x = glm::radians(r.x);
            rotate.y = glm::radians(r.y);
            rotate.z = glm::radians(r.z);
        }

        virtual void onPreDraw() = 0;
        virtual void onDraw() = 0;
        virtual void update() = 0;
        virtual void onInit() = 0;

        virtual ~Drawable() {}
        
        glm::vec3 pos;
        glm::vec3 rotate;
        glm::vec3 scale;
        
        bool visible = true;
    protected:
        Program& program;
        Uniform<UT::Matrix4> model;
    };
}
