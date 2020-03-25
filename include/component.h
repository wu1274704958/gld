#pragma once 

#include <program.hpp>
#include <data_mgr.hpp>
#include <uniform.hpp>
#include <node.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace gld{

    struct Component
    {
        virtual bool init(){return true;}
        virtual void attach_node(std::weak_ptr<Node<Component>> n){ node = n; }
        virtual void reset_node(){node.reset();}
        virtual void before_draw(){}
        virtual void draw(){}
        virtual void after_draw(){}
        virtual void update(){}
        virtual int64_t idx() { return 0;}
        virtual ~Component(){}
        std::shared_ptr<Node<Component>> get_node(){return node.lock();}
        protected:
        std::weak_ptr<Node<Component>> node;
    };

    struct Render : public Component
    {
        Render(std::string vert_path,std::string frag_path) 
            : vert_path(std::move(vert_path)) , frag_path(std::move(frag_path)) 
        {}
        bool init() override;
        void before_draw() override;
        void after_draw() override;
        int64_t idx() override;
        std::shared_ptr<Program> get();
        std::string vert_path,frag_path;
    protected:
        std::shared_ptr<Program> program;
    };

    struct RenderEx : public Component
    {
        RenderEx(std::string vert_path,std::string frag_path,std::string geom_path) : 
            vert_path(std::move(vert_path)) , frag_path(std::move(frag_path)) ,geom_path(std::move(geom_path))
        {}
        bool init() override;
        void before_draw() override;
        void after_draw() override;
        int64_t idx() override;
        std::shared_ptr<Program> get();
        std::string vert_path,frag_path,geom_path;
    protected:
        std::shared_ptr<Program> program;
    };
    
    
    struct Transform : public Component
    {
        Transform(std::string model_key);
        Transform();

        bool init() override ;

        void draw() override ;

        glm::mat4 get_model();

        virtual void update_matrix(glm::mat4 mat);

        void setRotateX(float x) ;
        void setRotateY(float x) ;
        void setRotateZ(float x) ;
        void setRotation(glm::vec3 r) ;

        glm::vec3 pos;
        glm::vec3 rotate;
        glm::vec3 scale;
    protected:
        Uniform<UT::Matrix4> model;
        bool no_render = false;
    };
    
}