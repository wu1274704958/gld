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
#include <ui/word.h>


using namespace gld;
namespace fs = std::filesystem;

using  namespace dbg::literal;

using namespace wws;
using namespace gen;
using namespace txt;
using namespace sundry;
using namespace evt;

class Demo1 : public RenderDemoRotate {
public:
    Demo1() : perspective("perspective"), world("world"), fill_color("fill_color"),
        event_dispatcher(*perspective,(*world),width,height,
             glm::vec3(0.f, 0.f, 0.f), glm::vec3(0.f, 0.f, -2.f))
        {}
    int init() override
    {
        event_dispatcher.child = [=](int i)->EventHandler<Node<Component>>* {
            try {
                return dynamic_cast<evt::EventHandler<Node<Component>>*>(cxts[i].get());
            }
            catch (std::bad_cast& e) { return nullptr; }
            return nullptr;
        };
        event_dispatcher.childlen_count = [=]()->int
        {
            return static_cast<int>(cxts.size());
        };

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
            auto a = std::shared_ptr<Node<Component>>(new Word(font, 126, k));
            auto w = dynamic_cast<Word*>(a.get());
            w->load();
            w->add_listener(EventType::Click, [w](Event<Node<Component>>* e)->bool {
                auto mater = w->get_comp<DefTextMaterial>();
                mater->color = glm::vec4(rd_0_1(), rd_0_1(), rd_0_1(), rd_0_1());
                return true;
            });
            auto trans = a->get_comp<Transform>();
            trans->pos = glm::vec3((rd_0_1() - 0.5f) * 5.f, (rd_0_1() - 0.5f) * 5.f, (rd_0_1() - 0.5f) * 5.f);
            auto mater = a->get_comp<DefTextMaterial>();
            mater->color = glm::vec4(rd_0_1(), rd_0_1(), rd_0_1(), rd_0_1());
            cxts.push_back(a);
        }

        for (auto k = L'Œ‚'; k <= L'Œ‚' + 300; ++k)
        {
            auto a = std::shared_ptr<Node<Component>>(new Word(font2, 126, k));
            auto w = dynamic_cast<Word*>(a.get());
            w->load();
            w->add_listener(EventType::Click, [w](Event<Node<Component>>* e)->bool {
                auto mater = w->get_comp<DefTextMaterial>();
                mater->color = glm::vec4(rd_0_1(), rd_0_1(), rd_0_1(), rd_0_1());
                return true;
            });
            auto trans = a->get_comp<Transform>();
            trans->pos = glm::vec3((rd_0_1() - 0.5f) * 5.f, (rd_0_1() - 0.5f) * 5.f, (rd_0_1() - 0.5f) * 5.f);
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
        perspective = glm::perspective(glm::radians(60.f), ((float)width / (float)height), 0.1f, 1024.0f);
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
        if(action == GLFW_PRESS)
            event_dispatcher.onMouseDown(btn, mode, x, y);
        else
        if(action == GLFW_RELEASE)
            event_dispatcher.onMouseUp(btn, mode, x, y);
    }

    void onMouseMove(double x, double y) override
    {
        RenderDemoRotate::onMouseMove(x, y);
        event_dispatcher.onMouseMove(static_cast<int>(x), static_cast<int>(y));
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
    EventDispatcher<Node<Component>> event_dispatcher;
    
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