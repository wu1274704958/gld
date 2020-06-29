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
             glm::vec3(0.f, 0.f, 0.f),camera_dir, glm::vec3(0.f, 0.f, -2.f))
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

        ClipNode<Component>::enable();

        

        auto onMove = [=](Event<Node<Component>>* e)->bool {
            auto w = e->target.lock();
            auto p = reinterpret_cast<MouseEvent<Node<Component>>*>(e);
            if (w && p->btn == GLFW_MOUSE_BUTTON_2)
            {
                auto tra = w->get_comp<Transform>();
                auto of = p->pos - down_pos; of.z = 0.f;
                tra->pos += of;
                down_pos = p->pos;
                dbg(std::make_tuple(tra->pos.x,tra->pos.y));
                return true;
            }
            return false;
        };

        auto onDown = [=](Event<Node<Component>>* e)->bool {
            auto w = e->target.lock();
            auto p = reinterpret_cast<MouseEvent<Node<Component>>*>(e);
            if (w && p->btn == GLFW_MOUSE_BUTTON_2)
                down_pos = p->pos;
            return true;
        };

        curr_play = std::shared_ptr<Label>(new Label(760 * 6,760));
        curr_play->font = font2;
        curr_play->auto_scroll = true;
        curr_play->create();
        curr_play->color = wws::make_rgba(PREPARE_STRING("009E8EFF")).make<glm::vec4>();
        curr_play->size = 760;
        curr_play->get_comp<Transform>()->pos = glm::vec3(-7.03303f, 3.71308f, -5.f);
        curr_play->set_text("请点击你喜欢的歌曲");
        curr_play->add_listener(EventType::MouseDown, onDown);
        curr_play->add_listener(EventType::MouseMove, onMove);

        cxts.push_back(curr_play);

        auto onclick = [](Event<Node<Component>>* e)->bool {
            auto w = e->target.lock();
            if (w)
            {
                auto mater = w->get_comp<DefTextMaterial>();
                mater->color = glm::vec4(rd_0_1(), rd_0_1(), rd_0_1(), rd_0_1());
                return true;
            }
            return false;
        };

        list_ui = std::shared_ptr<Sphere>(new Sphere(36,31));
        list_ui->create();
        cxts.push_back(list_ui);
        

        list_ui->onAddOffset = [](const std::shared_ptr<Node<Component>>& c)->glm::vec3
        {
            auto p = dynamic_cast<Label*>(c.get());
            return glm::vec3(p->get_width() / -2.f, p->get_height() / 2.f, 0.f);
        };

        push_names(list_ui,{"这个歌名超级长啊啊啊啊!", "断桥残雪","大时代","阿萨德","哦IP技术","陪我i看到","北京","白蛇传"
            ,"断桥残雪","大时代","阿萨德","哦IP技术","陪我i看到","北京","白蛇传" 
            ,"断桥残雪","大时代","阿萨德","哦IP技术","陪我i看到","北京","白蛇传" 
            ,"断桥残雪","大时代","阿萨德","哦IP技术","陪我i看到","北京","白蛇传" 
            ,"断桥残雪","大时代","阿萨德","哦IP技术","陪我i看到","北京","白蛇传" 
            ,"断桥残雪","大时代","阿萨德","哦IP技术","陪我i看到","北京","白蛇传",
            "断桥残雪","大时代","阿萨德","哦IP技术","陪我i看到","北京","白蛇传" });
       
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_FRAMEBUFFER_SRGB);

         for (auto& p : cxts)
            p->init();

        return 0;
    }

    void push_names(std::shared_ptr<Sphere>& sp,std::vector<std::string>&& v)
    {
        int i = 0;
        for (auto& s : v)
        {
            auto label = std::shared_ptr<Label>(new Label());
            label->auto_scroll = true;
            label->font = font2;
            label->color = glm::vec4(rd_0_1(), rd_0_1(), rd_0_1(),rd_0_1());
            label->align = Align::Center;
            label->size = 32;
            label->onTextSizeChange = [&label](float w, float h)
            {
                constexpr float max_w = 32.f * 4.f * Word::WORD_SCALE;
                label->set_size_no_scale(w > max_w ? max_w : w , h);
                label->refresh();
            };
            label->set_text(s);
            label->add_listener(EventType::Click, [=](Event<Node<Component>>* e)->bool {
                return this->onClickPlay(e);
            });
            sp->rand_add(label);
        }
    }

    std::shared_ptr<Node<Component>> create_word(std::string& font, uint32_t k, 
        std::function<bool(Event<Node<Component>>*)> onclick,
        std::function<bool(Event<Node<Component>>*)> onMove,
        std::function<bool(Event<Node<Component>>*)> onDown
        ,int size = 126,float ox = 0.5f,float oy = 0.5f)
    {
        auto a = std::shared_ptr<Node<Component>>(new Word(font, size, k,glm::vec2(ox,oy)));
        auto w = dynamic_cast<Word*>(a.get());
        w->load();
        w->add_listener(EventType::Click, onclick);
        w->add_listener(EventType::MouseMove, onMove);
        w->add_listener(EventType::MouseDown, onDown);
        auto trans = a->get_comp<Transform>();
        trans->pos = glm::vec3((rd_0_1() - 0.5f) * 5.f, (rd_0_1() - 0.5f) * 5.f, (rd_0_1() - 0.5f) * 5.f);
        auto mater = a->get_comp<DefTextMaterial>();
        mater->color = glm::vec4(rd_0_1(), rd_0_1(), rd_0_1(), rd_0_1());
        return a;
    }

    bool onClickPlay(Event<Node<Component>>* e)
    {
        auto tar = e->target.lock();
        auto p = dynamic_cast<Label*>(tar.get());
        curr_play->set_text(p->text);
        for (auto& c : curr_play->get_children())
        {
            c->init();
        }
        return true;
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
    glm::vec3 down_pos,camera_dir = glm::vec3(0.f,0.f,-1.f);
    std::shared_ptr<Sphere> list_ui;
    std::shared_ptr<Label> curr_play;
    std::string font = "fonts/SHOWG.TTF";
    std::string font2 = "fonts/happy.ttf";
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