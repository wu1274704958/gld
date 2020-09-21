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


class Demo1 : public RenderDemoRotate {
public:
    Demo1() : perspective("perspective"), world("world"), fill_color("fill_color")
        {}
    int init() override
    {
        RenderDemoRotate::init();
        
        program = DefDataMgr::instance()->load<DataType::ProgramWithGeometry>("base/line_vs.glsl","base/line_fg.glsl","base/line_ge.glsl");
        program->use();

        program->locat_uniforms("perspective", "world", "model", "fill_color");

        perspective.attach_program(program);
        world.attach_program(program);
        fill_color.attach_program(program);

        fill_color = glm::vec3(0.f,1.0f,0.f);
        fill_color.sync();

        std::vector<glm::vec3> v = gen::circle(0.f,1.f,128);

        //std::vector<glm::vec3> v = { glm::vec3(1.f,0.f,0.f),glm::vec3(-1.f,0.f,0.f) };

        auto vao = std::make_shared<gld::VertexArr>();
        vao->create();
        vao->create_arr<gld::ArrayBufferType::VERTEX>();
        vao->bind();
        vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(v, GL_STATIC_DRAW);
        vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
            gld::VAP_DATA<3, float, false>>();
        vao->unbind();

        auto mesh = curved_surface(0.7f,0.8f,0.15f,0.08f,32);

        mesh->vao = vao;

        mesh->mode = GL_LINES;

        auto node = std::make_shared<Node<Component>>();
        node->add_comp<Transform>(std::make_shared<Transform>());
        node->get_comp<Transform>()->scale = glm::vec3(2.7f, 2.7f, 2.7f);
        node->add_comp<def::Mesh>(mesh);
        node->add_comp<Render>(std::make_shared<Render>("base/line_vs.glsl","base/line_fg.glsl", "base/line_ge.glsl"));

        cxts.push_back(node);
        
        glEnable(GL_FRAMEBUFFER_SRGB);
        glEnable(GL_DEPTH_TEST);

        glEnable(GL_BLEND);
		glEnable(GL_ALPHA);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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

        world = glm::translate(*world, glm::vec3(0.f,0.f, -5.0f));
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

    void onMouseButton(int btn,int action,int mode,double x,double y) override
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
};

#ifndef PF_ANDROID
int main()
{
    fs::path root = wws::find_path(3, "res", true);
    ResMgrWithGlslPreProcess::create_instance(root);
    DefResMgr::create_instance(std::move(root));
    Demo1 d;
    if (d.initWindow(1200, 800, "Clock"))
    {
        printf("init window failed\n");
        return -1;
    }
    d.init();
    d.run();

    return 0;
}

#endif