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
#include <text/TextMgr.hpp>
#include <sundry.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <event/EventDispatcher.h>


using namespace gld;
namespace fs = std::filesystem;

using  namespace dbg::literal;

using namespace wws;
using namespace gen;
using namespace txt;
using namespace sundry;


class Demo1 : public RenderDemoRotate {
public:
    Demo1() : perspective("perspective"), world("world"), fill_color("fill_color")
        {}
    int init() override
    {
        RenderDemoRotate::init();
        
        program = DefDataMgr::instance()->load<DataType::Program>("base/line_vs.glsl","base/line_fg.glsl");
        program->use();

        program->locat_uniforms("perspective", "world", "model", "fill_color");

        perspective.attach_program(program);
        world.attach_program(program);
        fill_color.attach_program(program);

        fill_color = glm::vec3(0.f,1.0f,0.f);
        fill_color.sync();

        std::string font = "fonts/SHOWG.TTF";
        std::string font2 = "fonts/SIMHEI.TTF";

        auto mesh = curved_surface(0.7f,0.8f,0.15f,0.08f,32);

        /*mesh->mode = GL_TRIANGLE_STRIP;

        auto node = std::make_shared<Node<Component>>();
        node->add_comp<Transform>(std::make_shared<Transform>());
        node->add_comp<def::Mesh>(mesh);
        node->add_comp<Render>(std::make_shared<Render>("base/line_vs.glsl","base/line_fg.glsl"));

        cxts.push_back(node);*/

        for (auto k = L'!'; k <= L'~'; ++k)
        {
            auto [a, wd, size] = DefTexMgr::instance()->get_node(font, 0, 0, 126, k);
            auto trans = a->get_comp<Transform>();
            trans->pos = glm::vec3((rd_0_1() - 0.5f) * 5.f, (rd_0_1() - 0.5f) * 5.f, (rd_0_1() - 0.5f) * 5.0f);
            trans->scale = glm::vec3(0.1f);
            auto mater = a->get_comp<DefTextMaterial>();
            mater->color = glm::vec4(rd_0_1(), rd_0_1(), rd_0_1(), rd_0_1());
            cxts.push_back(a);
        }

        for (auto k = L'Œ‚'; k <= L'Œ‚' + 300; ++k)
        {
            auto [a, wd, size] = DefTexMgr::instance()->get_node(font2, 0, 0, 126, k);
            auto trans = a->get_comp<Transform>();
            trans->pos = glm::vec3((rd_0_1() - 0.5f) * 5.f, (rd_0_1() - 0.5f) * 5.f, (rd_0_1() - 0.5f) * 5.0f);
            trans->scale = glm::vec3(0.1f);
            auto mater = a->get_comp<DefTextMaterial>();
            mater->color = glm::vec4(rd_0_1(), rd_0_1(), rd_0_1(), rd_0_1());
            cxts.push_back(a);
        }

        /*auto [a, wd, size] = DefTexMgr::instance()->get_node(font2, 0, 0, 126, L'≈¿',1.f,0.f);
        auto trans = a->get_comp<Transform>();
        auto mater = a->get_comp<DefTextMaterial>();
        mater->color = glm::vec4(rd_0_1(), rd_0_1(), rd_0_1(), rd_0_1());
        cxts.push_back(a);*/
       
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_FRAMEBUFFER_SRGB);

         for (auto& p : cxts)
            p->init();

        return 0;
    }


    void draw() override
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        for (auto n : cxts)
        {
            auto render = n->get_comp<Render>();
            auto p = render->get();
            p->use();
            perspective.attach_program(p);
            world.attach_program(p);

            perspective.sync();
            world.sync();
        }

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

    void onMouseButton(int btn,int action,int mode,int x,int y) override
    {
        RenderDemoRotate::onMouseButton(btn,action,mode,x,y);
        float nx, ny;
        sundry::screencoord_to_ndc(width, height, x, y, &nx, &ny);
        dbg(std::make_tuple(nx,ny));

        auto t_world = glm::mat4(1.0f);
        t_world = glm::translate(t_world, glm::vec3(0.f, 0.f, -2.0f));

        glm::vec3 raypos, raydir;
        sundry::normalized2d_to_ray(nx, ny, glm::inverse(t_world * (*perspective)  ), glm::vec3(0.f, 0.f, 0.0f), raypos, raydir);

        dbg(std::make_tuple(raypos.x, raypos.y, raypos.z));
        dbg(std::make_tuple(raydir.x, raydir.y, raydir.z));
        for (auto& p : cxts)
        {
            glm::vec2 braypos; float distance; glm::vec3 pos;
            auto mesh = p->get_comp<gld::def::MeshRayTest>();
            
            if (mesh->ray_test(*world, glm::vec3(0.f, 0.f, 0.0f), raydir, braypos, distance,pos))
            {
                dbg(std::make_tuple(braypos.x, braypos.y, distance));
                auto mater = p->get_comp<txt::DefTextMaterial>();
                mater->color = glm::vec4(sundry::rd_0_1(), sundry::rd_0_1(), sundry::rd_0_1(), sundry::rd_0_1());
            }
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
    GlmUniform<UT::Vec3> fill_color;
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