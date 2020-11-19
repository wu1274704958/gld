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
#include <ui/clip.h>
#include <ui/label.h>
#include <ui/sphere.h>
#include <ui/word_patch.h>


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
        event_dispatcher(*perspective, (*world), width, height,
            glm::vec3(0.f, 0.f, 0.f), camera_dir, glm::vec3(0.f, 0.f, -2.f))
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
        event_dispatcher.skip_min_distance = 0.1f;

        RenderDemoRotate::init();

        auto res = gen::sphere(0.5,10,3);

        ClipNode<Component>::enable();
        auto list_ui = std::shared_ptr<Sphere>(new Sphere(15, 16, 0.1f));
        list_ui->create();
        auto n = list_ui->slot_count();
        cxts.push_back(list_ui);
        std::string font = "fonts/SHOWG.TTF";

        list_ui->onSlotChanged = [](size_t i, std::shared_ptr<Node<Component>>& c) {
            float depth = (c->get_comp<Transform>()->pos.z + 0.5f);
            auto p = dynamic_cast<Label*>(c.get());
            c->get_comp<Transform>()->scale.x = depth;
            c->get_comp<Transform>()->scale.y = depth;
        };
        
        std::vector<std::string> strs;
        
        for(int i = 0;i < n;++i)
        {
            int c = rand();
            auto str = wws::to_string(c);
            strs.push_back(str);
        }
        
        push_names(list_ui,std::move(strs));
        
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_FRAMEBUFFER_SRGB);

        for (auto& p : cxts)
            p->init();

        return 0;
    }

    void push_names(std::shared_ptr<Sphere>& sp, std::vector<std::string>&& v)
    {
        int i = 0;
        for (auto& s : v)
        {
            auto node = std::make_shared<Node<Component>>();
            node->add_comp<Transform>(std::make_shared<Transform>());
            auto label = std::shared_ptr<Label>(new Label());
            label->font = "fonts/SHOWG.TTF";
            label->color = glm::vec4(rd_0_1(), rd_0_1(), rd_0_1(), rd_0_1());
            label->align = Align::Center;
            label->size = 36;
            label->set_text(s);
            label->onTextSizeChange = [&label](float w,float h)
            {
                auto trans = label->get_comp<Transform>();
                trans->pos.x = w / -2.f;
                trans->pos.y = h / 2.f;
            };
            node->add_child(label);
            sp->rand_add(node);
        }
    }

    std::shared_ptr<Node<Component>> create_word(std::string& font, uint32_t k
        , int size = 126, float ox = 0.5f, float oy = 0.5f)
    {
        auto a = std::shared_ptr<Node<Component>>(new Word(font, size, k, glm::vec2(ox, oy)));
        auto w = dynamic_cast<Word*>(a.get());
        w->load();
        auto trans = a->get_comp<Transform>();
        trans->pos = glm::vec3((rd_0_1() - 0.5f) * 5.f, (rd_0_1() - 0.5f) * 5.f, (rd_0_1() - 0.5f) * 5.f);
        auto mater = a->get_comp<DefTextMaterial>();
        mater->color = glm::vec4(rd_0_1(), rd_0_1(), rd_0_1(), rd_0_1());
        return a;
    }

    void draw() override
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        update_mat(cxts);

        for (auto& p : cxts)
            p->draw();

        update();
        update_matrix();

    }

    void update_mat(std::vector<std::shared_ptr< gld::Node<gld::Component>>>& ns)
    {
        for (auto& n : ns)
        {
            auto render = n->get_comp<Render>();
            if (render)
            {
                auto p = render->get();
                if (p)
                {
                    p->use();
                    perspective.attach_program(p);
                    world.attach_program(p);

                    perspective.sync();
                    world.sync();
                }
            }
            if (n->children_count() > 0)
                update_mat(n->get_children());
        }
    }

    void update_matrix()
    {
        perspective = glm::perspective(glm::radians(60.f), ((float)width / (float)height), 0.1f, 1024.0f);
        world = glm::mat4(1.0f);

        world = glm::translate(*world, glm::vec3(0.f, 0.f, -2.0f));
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

    void onMouseButton(int btn, int action, int mode, double x, double y) override
    {
        RenderDemoRotate::onMouseButton(btn, action, mode, x, y);
        if (action == GLFW_PRESS)
            event_dispatcher.onMouseDown(btn, mode, x, y);
        else
            if (action == GLFW_RELEASE)
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
    glm::vec3 down_pos, camera_dir = glm::vec3(0.f, 0.f, -1.f);

};

#ifndef PF_ANDROID
int main()
{
    fs::path root = wws::find_path(3, "res", true);
    ResMgrWithGlslPreProcess::create_instance(root);
    DefResMgr::create_instance(std::move(root));
    Demo1 d;
    if (d.initWindow(1800, 960, "Clock"))
    {
        printf("init window failed\n");
        return -1;
    }
    d.init();
    d.run();

    return 0;
}

#endif