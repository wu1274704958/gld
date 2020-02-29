#pragma once 

#include <program.hpp>
#include <data_mgr.hpp>
#include <uniform.hpp>
#include <node.hpp>

namespace gld{

    struct Component{
        virtual bool init(){return true;}
        virtual void attach_node(Node<Component>* n){ node = n; }
        virtual void on_draw(){}
        virtual void draw(){}
        virtual void update(){}
        virtual int64_t idx() { return 0;}
        virtual ~Component(){}
        Node<Component>* node;
    };

    struct Render : public Component
    {
        Render(std::string vert_path,std::string frag_path) : vert_path(vert_path) , frag_path(frag_path) 
        {}
        bool init() override 
        {
            program = DefDataMgr::instance()->load<DataType::Program>(vert_path,frag_path);
            return (bool)program;
        }
        void on_draw() override
        {
            program->use();
        }
        int64_t idx() { return -100;}
        std::shared_ptr<Program> get()
        {
            return program;
        }
        std::string vert_path,frag_path;
    protected:
        std::shared_ptr<Program> program;
    };
    
    
    struct Transform : public Component
    {
        Transform(std::string model_key) : model(std::move(model_key))
        {
            rotate = pos = glm::vec3(0.f, 0.f, 0.f);
            scale = glm::vec3(1.f, 1.f, 1.f);
        }

        bool init() override 
        {
            auto render = node->get_comp<Render>();
            model.attach_program(render->get());
            return true;
        }

        void on_draw() override 
        {
            glm::mat4 model(1.0f);

            model = glm::translate(model, pos);

            model = glm::scale(model, scale);

            model = glm::rotate(model, rotate.x, glm::vec3(1.f, 0.f, 0.f));
            model = glm::rotate(model, rotate.y, glm::vec3(0.f, 1.f, 0.f));
            model = glm::rotate(model, rotate.z, glm::vec3(0.f, 0.f, 1.f));

            update_matrix(model);
        }

        virtual void update_matrix(glm::mat4 mat)
        {
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
        void setRotation(glm::vec3 r) {
            rotate.x = glm::radians(r.x);
            rotate.y = glm::radians(r.y);
            rotate.z = glm::radians(r.z);
        }

        glm::vec3 pos;
        glm::vec3 rotate;
        glm::vec3 scale;
    protected:
        Uniform<UT::Matrix4> model;
    };

    struct Mesh : public Component {
        
    };
    
}