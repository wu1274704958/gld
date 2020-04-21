#include <resource_mgr.hpp>
#include <FindPath.hpp>
#include <glad/glad.h>
#include <RenderDemoRotate.hpp>
#include <cstdio>
#include <memory>
#include <macro.hpp>
#include <sundry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <dbg.hpp>
#include <vector>
#include <make_color.hpp>
#include <gl_comm.hpp>
#include <drawable.h>
#include <program.hpp>
#include <vertex_arr.hpp>
#include <uniform.hpp>
#include <texture.hpp>
#include <comm.hpp>
#include <random>
#include <uniform_buf.hpp>
#include <log.hpp>
#include <assimp/scene.h>
#include <data_mgr.hpp>
#include <component.h>
#include <comps/Material.hpp>
#include <frame_rate.h>

using namespace gld;
namespace fs = std::filesystem;

using  namespace dbg::literal;

struct Material:public Component
{
    Material( std::shared_ptr<Texture<TexType::D2>> diffuseTex):
        udiffuseTex("diffuseTex"),
        //uColor("fill_color"),
        diffuseTex(std::move(diffuseTex))
    {
        udiffuseTex = 0;
        //uColor = glm::vec3(1.f,1.f,1.f);
    }
    bool init() override
    {
        bool f = true;
        if(diffuseTex)
        {
            diffuseTex->bind();
            diffuseTex->generate_mipmap();
            diffuseTex->set_paramter<TexOption::WRAP_S,TexOpVal::REPEAT>();
            diffuseTex->set_paramter<TexOption::WRAP_T,TexOpVal::REPEAT>();
            diffuseTex->set_paramter<TexOption::MIN_FILTER,TexOpVal::LINEAR_MIPMAP_LINEAR>();
            diffuseTex->set_paramter<TexOption::MAG_FILTER,TexOpVal::LINEAR>();
            f = false;
        }
        auto n_ptr = get_node();
        auto render = n_ptr->get_comp<Render>();
        udiffuseTex.attach_program(render->get());
        //uColor.attach_program(render->get());
        return f;
    }
    void draw() override
    {
        if(diffuseTex)
            diffuseTex->active<ActiveTexId::_0>();
        //uColor.sync();
        udiffuseTex.sync();
    }
    void after_draw() override
    {
        diffuseTex->unbind();
    }
   
    GlmUniform<UT::Sampler2D>   udiffuseTex;
    //GlmUniform<UT::Vec3>        uColor;
    std::shared_ptr<Texture<TexType::D2>> diffuseTex;
};

struct Point{
    glm::mat4 model;
    glm::vec3 color;

    Point() : model(1.f), color(1.f,1.f,1.f){}
};

class Demo1 : public RenderDemoRotate {
public:
    Demo1() : perspective("perspective"), world("world")
        {}
    int init() override
    {
        RenderDemoRotate::init();
        
        program = DefDataMgr::instance()->load<DataType::Program>("point/base_vs.glsl","point/base_fg.glsl");
        program->use();

        program->locat_uniforms("perspective", "world", "model", "diffuseTex");

        perspective.attach_program(program);
        world.attach_program(program);

        float planeVertices[] = {
            // positions          //normal            // texture Coords 
             0.5f, 0.5f, 0.f,   0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
            -0.5f, 0.5f, 0.f,   0.0f,  1.0f,  0.0f,  0.0f, 0.0f,
            -0.5f,-0.5f, 0.f,   0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
  
             0.5f, 0.5f, 0.f,   0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
            -0.5f,-0.5f, 0.f,   0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
             0.5f,-0.5f, 0.f,   0.0f,  1.0f,  0.0f,  1.0f, 1.0f
        };

        auto plane_vao = std::make_shared<gld::VertexArr>();
        plane_vao->create();
        plane_vao->create_arr<gld::ArrayBufferType::VERTEX>();
        plane_vao->bind();
        plane_vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(planeVertices,GL_STATIC_DRAW);
        plane_vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
         gld::VAP_DATA<3,float,false>,
         gld::VAP_DATA<3,float,false>,
         gld::VAP_DATA<2,float,false>>();
        plane_vao->unbind();

        plane_mesh = std::shared_ptr<def::MeshInstanced>(new def::MeshInstanced(0,wws::arrLen(planeVertices)/8, pw * ph ,plane_vao ));

        dif_plane = DefDataMgr::instance()->load<DataType::Texture2D>("textures/circle.png",0);

        bx = -static_cast<float>(pw) / 2.f; 
        by = -static_cast<float>(ph) / 2.f;

        cxts.push_back(create_point());

        auto ps = verex_data_surface();

        sync_vertex_data(ps);
        
        // glEnable(GL_BLEND);
        // glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

        glDisable(GL_CULL_FACE);
        //glCullFace(GL_BACK);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        GLenum err = glGetError();
        dbg(err);

        for (auto& p : cxts)
            p->init();
        update_matrix();
        return 0;
    }

    std::shared_ptr<Node<Component>> create_point(glm::vec3 pos = glm::vec3(0.f,0.f,0.f))
    {
        std::shared_ptr<Node<Component>> res = std::make_shared<Node<Component>>();
        res->add_comp<Transform>(std::make_shared<Transform>());
        res->get_comp<Transform>()->pos = pos;
        res->add_comp(plane_mesh);
        res->add_comp<Render>(std::shared_ptr<Render>(new Render("point/base_vs.glsl","point/base_fg.glsl")));
        res->add_comp<Material>(std::shared_ptr<Material>(new Material(dif_plane)));
        return res;
    }

    Point create_point(glm::vec3 pos,glm::vec3 color)
    {
        Point res;
        res.color = color;
        res.model = glm::translate(res.model,pos);
        return res;
    }

    void sync_vertex_data(std::vector<Point>& ps)
    {
        auto& vao = plane_mesh->vao;
        vao->bind_self();
        auto& buf = vao->create_one();
        buf.bind_data(ps.data(), pw * ph ,GL_STATIC_DRAW);
        
        buf.vertex_attrib_pointer<3,
            VAP_DATA<4,float,false>,
            VAP_DATA<4,float,false>,
            VAP_DATA<4,float,false>,
            VAP_DATA<4,float,false>,
            VAP_DATA<3,float,false>
            >();

        vao->vertex_attr_div<3,1,1,1,1,1>();
        vao->unbind_self();
    }

    std::vector<Point> verex_data_surface()
    {
        std::vector<Point> ps;
        auto c = rd_vec3();

        for(int y = 0;y < ph;++y)
        {
            for(int x = 0;x < pw;++x)
            {
                auto c = rd_vec3();
                ps.push_back(create_point(glm::vec3((float)x + bx,(float)y + by,0.0f),c));
            }
        }
        return ps;
    }


    float rd_0_1()
    {
        std::random_device r;
        std::default_random_engine e1(r());
        std::uniform_int_distribution<int> uniform_dist(0, 1000000);
        return static_cast<float>(uniform_dist(e1)) / 100000.f;
    }

    glm::vec3 rd_vec3()
    {
        return glm::vec3(rd_0_1(),rd_0_1(),rd_0_1());
    }

    void draw() override
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        program->use();
        perspective.attach_program(program);
        world.attach_program(program);
      
        perspective.sync();
        world.sync();

        for (auto& p : cxts)
            p->draw();

        update();
        update_matrix();

    }

    void update_matrix()
    {
        perspective = glm::perspective(glm::radians(60.f), ((float)width / (float)height), 0.1f, 256.0f);
        world = glm::mat4(1.0f);

        world = glm::translate(*world, glm::vec3(0.f,0.f, -132.0f));
        world = glm::rotate(*world, glm::radians(rotate.x), glm::vec3(1.f, 0.f, 0.f));
        world = glm::rotate(*world, glm::radians(rotate.y), glm::vec3(0.f, 1.f, 0.f));
        world = glm::rotate(*world, glm::radians(rotate.z), glm::vec3(0.f, 0.f, 1.f));
    }

    void update()
    {
        for (auto& p : cxts)
            p->update();
    }

    ~Demo1() {

    }

    void onMouseButton(int btn,int action,int mode) override
    {
        RenderDemoRotate::onMouseButton(btn,action,mode);
        if(btn == GLFW_MOUSE_BUTTON_2 && action == GLFW_PRESS)
        {
            auto ps = verex_data_surface();

            sync_vertex_data(ps);
        }
    }

    void onWindowResize(int w, int h) override
    {
        glViewport(0, 0, w, h);
    }
private:
    std::shared_ptr<Program> program;
    GlmUniform<UT::Matrix4> perspective;
    GlmUniform<UT::Matrix4> world;
    std::vector<std::shared_ptr< gld::Node<gld::Component>>> cxts;
    std::shared_ptr<def::MeshInstanced> plane_mesh;
    //std::shared_ptr<Material> plane_mat;
    std::shared_ptr<Texture<TexType::D2>> dif_plane;
    std::shared_ptr<Render> render;
    int pw = 224,ph = 56;
    float bx,by;

};

#ifndef PF_ANDROID
int main()
{
    fs::path root = wws::find_path(3, "res", true);
    ResMgrWithGlslPreProcess::create_instance(root);
    DefResMgr::create_instance(std::move(root));
    Demo1 d;
    if (d.initWindow(800, 800, "Demo1"))
    {
        printf("init window failed\n");
        return -1;
    }
    d.init();
    d.run();

    return 0;
}

#endif