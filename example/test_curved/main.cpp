#define _CRT_SECURE_NO_WARNINGS
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
#include <strstream>
#include <make_color.hpp>
#include <generator/Generator.hpp>
#include "../lighting_6/model.hpp"
#include "../lighting_6/light.hpp"
#include <text/TextMgr.hpp>


using namespace gld;
namespace fs = std::filesystem;

using  namespace dbg::literal;

using namespace wws;
using namespace gen;

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

#define test_text 1

class Demo1 : public RenderDemoRotate {
public:
    Demo1() : perspective("perspective"), world("world"), fill_color("fill_color"), view_pos("view_pos")
        {}
    int init() override
    {
        RenderDemoRotate::init();

        glEnable(GL_DEPTH_TEST);
        
        program = DefDataMgr::instance()->load<DataType::Program>("base/curve_vs.glsl","base/curve_fg.glsl");
        program->use();

        program->locat_uniforms("perspective", "world", "model", "diffuseTex", "ambient_strength",
            "specular_strength",
            "view_pos",
            "shininess","specularTex");

        perspective.attach_program(program);
        world.attach_program(program);
        view_pos.attach_program(program);
        
        glm::vec3 vp(0.f,0.f,0.f );
        view_pos = glm::value_ptr(vp);

        light.init(GL_STATIC_DRAW);
        
        light->color = glm::vec3(1.f, 1.f, 1.f);
        light->dir = glm::vec3(-0.2f, -1.0f, -0.3f);

        light.sync(GL_MAP_WRITE_BIT| GL_MAP_INVALIDATE_BUFFER_BIT);

        auto mesh = curved_surface(0.7f,0.8f,0.15f,0.08f,32,test_text);

        mesh->mode = GL_TRIANGLE_STRIP;

        auto node = std::make_shared<Node<Component>>();
        node->add_comp<Transform>(std::make_shared<Transform>());
        node->add_comp<def::Mesh>(mesh);
        node->add_comp<Render>(std::make_shared<Render>("base/curve_vs.glsl","base/curve_fg.glsl"));

        auto dif_plane = DefDataMgr::instance()->load<DataType::Texture2D>("textures/wood.png", 0);
        auto spec_plane = DefDataMgr::instance()->load<DataType::Texture2D>("textures/wood_spec.png", 0);

        #if test_text
            std::string font = "fonts/SIMHEI.TTF";
            for (auto k = L'A'; k <= L'Z'; ++k)
                txt::DefTexMgr::instance()->put(font, 0, 0, 96, k);

            for (auto k = L'我'; k <= L'我' + 500; ++k)
            {
                txt::DefTexMgr::instance()->put(font, 0, 0, 96, k);
            }

            auto [tex, wd] = txt::DefTexMgr::instance()->get_texture(font, 0, 0, 96, L'C');

            auto plane_mat = std::shared_ptr<def::Material>(new def::Material(tex, spec_plane));
        #else 
            auto plane_mat = std::shared_ptr<def::Material>(new def::Material(dif_plane, spec_plane));
        #endif

        
        plane_mat->uambient_strength = 0.03f;
        plane_mat->ushininess = 256.f;
        plane_mat->uspecular_strength = 0.5f;

        node->add_comp<def::Material>(plane_mat);

        node->get_comp<Transform>()->scale = glm::vec3(1.64f);

        cxts.push_back(node);
        
        glEnable(GL_FRAMEBUFFER_SRGB);

         for (auto& p : cxts)
            p->init();

        return 0;
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

        world = glm::translate(*world, glm::vec3(0.f,0.f, -2.0f));
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

    void onMouseButton(int btn,int action,int mode, double x, double y) override
    {
        RenderDemoRotate::onMouseButton(btn,action,mode,x,y);
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
    GlmUniform<UT::Vec3> fill_color;
    UniformBuf<0,DictLight> light;
    Uniform<UT::Vec3> view_pos;
};

#ifndef PF_ANDROID
int main()
{
    fs::path root = wws::find_path(3, "res", true);
    ResMgrWithGlslPreProcess::create_instance(root);
    DefResMgr::create_instance(std::move(root));
    Demo1 d;
    if (d.initWindow(800, 460, "Clock"))
    {
        printf("init window failed\n");
        return -1;
    }
    d.init();
    d.run();

    return 0;
}

#endif