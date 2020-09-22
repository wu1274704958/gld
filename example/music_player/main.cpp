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
#include <tools/executor.h>
#include "MusicPlayer.h"
#include "Pumper.h"
#include "tools/tween.hpp"
#include "tools/app.h"
#include "view1.h"
#include "view2.h"
#include "ui/wheel.h"
#include "lrc/lrc_mgr.hpp"

#ifndef  PF_ANDROID
static std::pair<const char**, int> LaunchArgs;
#endif // ! PF_ANDROID

using namespace gld;
namespace fs = std::filesystem;

using  namespace dbg::literal;

using namespace fv;
using namespace wws;
using namespace gen;
using namespace txt;
using namespace sundry;
using namespace evt;

class Demo1 : public RenderDemoRotate {
public:
    Demo1() : perspective("perspective"), world("world"), fill_color("fill_color"),
        event_dispatcher(*perspective,(*world),width,height,
             glm::vec3(0.f, 0.f, 0.f),camera_dir, glm::vec3(0.f, 0.f, -2.f)),
            pumper(player)
        {
        auto p = new float[4096];
        std::memset(p, 0, sizeof(float) * 4096); 
        fft_ptr = std::unique_ptr<float[]>(p);
    }
    int init() override
    {
        if (LaunchArgs.second < 2)
            return -1;

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

        init_curr_play();

        init_list_ui();

        init_flywheel();
        
        init_btns();
        //glPointSize(2.4f);
        //auto v1 = std::make_shared<View1>();
        //v1->create();
        
        //std::weak_ptr<Transform> tra = v1->get_comp_ex<Transform>();
        //App::instance()->tween.to(tra, &Transform::pos, &glm::vec3::z, 1600.f, 0.f, -5.6f, tween::Expo::easeInOut);
        //App::instance()->tween.to(tra, &Transform::rotate,&glm::vec3::x,1600.f, 0.f, 0.3f, tween::Expo::easeInOut);
        //cxts.push_back(v1);
        glLineWidth(1.2f);
        glPointSize(2.4f);
        glEnable(GL_POINT_SMOOTH);
        glHint(GL_POINT_SMOOTH, GL_NICEST);
        glEnable(GL_LINE_SMOOTH);
        glHint(GL_LINE_SMOOTH, GL_NICEST);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_FRAMEBUFFER_SRGB);

         for (auto& p : cxts)
            p->init();

        init_pumper();
        pumper.setMode(Pumper::LOOP);
        glClearColor(0.f, 0.f, 0.f, 0.f);

        return 0;
    }

    void init_pumper()
    {
        pumper.init(LaunchArgs.first[1]);
        pumper.onAutoPlay = [this](const MMFile& f)
        {
            this->lrc_mgr.onplay(f);
            this->curr_play_ani(f.get_name());
        };
    }

    void push_names(std::shared_ptr<Sphere>& sp,std::vector<std::string>&& v)
    {
        int idx = 0;
        for (auto& s : v)
        {
            push_name(sp,s,idx++);
        }
    }

    void list_ani(std::function<void()> func)
    {
        
        constexpr float dur = 500.f;
        std::function<glm::vec3(float)> f = k2vec3;
        std::weak_ptr<Transform> tra = list_ui->get_comp_ex<Transform>();
        App::instance()->tween.to(tra, &Transform::rotate, &glm::vec3::z, dur, 0.f, glm::pi<float>(), tween::Circ::easeOut);
        App::instance()->tween.to(tra, &Transform::scale, dur, music_list_scale, 0.f, tween::Circ::easeOut, f, [this, tra, f,dur,func]() {
            func();
            App::instance()->tween.to(tra, &Transform::scale, dur, 0.f, this->music_list_scale, tween::Circ::easeIn, f);
            App::instance()->tween.to(tra, &Transform::rotate, &glm::vec3::z, dur, glm::pi<float>(), 0.f, tween::Circ::easeIn);
        });
    }

    static glm::vec3 k2vec3(float v) {
        return glm::vec3(v, v, v);
    }

    template<typename T>
    void push_name(std::shared_ptr<Sphere>& sp, T&& v,int idx)
    {
        auto label = std::shared_ptr<Label>(new Label());
        label->auto_scroll = true;
        label->font = font2;
        label->color = glm::vec4(rd_0_1(), rd_0_1(), rd_0_1(), rd_0_1());
        label->align = Align::Center;
        label->size = 32.f * static_cast<int>( floor(music_list_scale) );
        label->onTextSizeChange = [sp,label, this](float w, float h)
        {
            App::instance()->exec.delay([this,sp,label, w, h]() {
                float max_w = 32.f * static_cast<int>( floor(music_list_scale) ) * 9.f * Word::WORD_SCALE;
                label->set_size_no_scale(w > max_w ? max_w : w, h);
                label->refresh();
                sp->rand_add(label);
            });
        };
        label->set_user_data((int*)idx);
        label->set_text(std::forward<T>(v));
        label->add_listener(EventType::Click, [=](Event<Node<Component>>* e)->bool {
            return this->onClickPlay(e);
        });
        label->init();
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
        dbg(p->text);
        auto new_text = p->text;     
        int64_t idx = (int64_t)(p->get_user_data<int*>());
        pumper.onclick(idx);
        return true;
    }

    template<typename T>
    void curr_play_ani(T text)
    {
        constexpr float dur = 500.f;
        std::weak_ptr<Transform> tra = curr_play->get_comp_ex<Transform>();
        App::instance()->tween.to(tra, &Transform::pos, &glm::vec3::z, dur, -5.f, 0.f, tween::Circ::easeInOut, [this, tra, dur, n_text = std::move(text)]() mutable {
            curr_play->set_text(n_text);
            App::instance()->tween.to(tra, &Transform::pos, &glm::vec3::z, dur, 0.f, -5.f, tween::Circ::easeInOut);
        });
    }

    void camera_reset()
    {
        if (rotate.x != 0.f)
            App::instance()->tween.to(std::ref(rotate), &glm::vec3::x, 1000.f, rotate.x, rotate.x > 180.f ? 360.f : 0.f, tween::Circ::easeOut);
        if (rotate.y != 0.f)
            App::instance()->tween.to(std::ref(rotate), &glm::vec3::y, 1000.f, rotate.y, rotate.y > 180.f ? 360.f : 0.f, tween::Circ::easeOut);
        if (rotate.z != 0.f)
            App::instance()->tween.to(std::ref(rotate), &glm::vec3::z, 1000.f, rotate.z, rotate.z > 180.f ? 360.f : 0.f, tween::Circ::easeOut);
    }

    void init_curr_play()
    {
        onMove = [=](Event<Node<Component>>* e)->bool {
            auto w = e->target.lock();
            auto p = reinterpret_cast<MouseEvent<Node<Component>>*>(e);
            if (w && p->btn == GLFW_MOUSE_BUTTON_2)
            {
                auto tra = w->get_comp<Transform>();
                auto of = p->pos - down_pos; of.z = 0.f;
                tra->pos += of;
                down_pos = p->pos;
                dbg(std::make_tuple(tra->pos.x, tra->pos.y));
                return true;
            }
            return false;
        };

        onDown = [=](Event<Node<Component>>* e)->bool {
            auto w = e->target.lock();
            auto p = reinterpret_cast<MouseEvent<Node<Component>>*>(e);
            if (w && p->btn == GLFW_MOUSE_BUTTON_2)
                down_pos = p->pos;
            return true;
        };

        curr_play = std::shared_ptr<Label>(new Label(760 * 12, 760));
        curr_play->font = font2;
        curr_play->auto_scroll = true;
        curr_play->create();
        curr_play->color = wws::make_rgba(PREPARE_STRING("009E8EFF")).make<glm::vec4>();
        curr_play->size = 760;
        curr_play->get_comp<Transform>()->pos = glm::vec3(-7.03303f, 3.71308f, -5.f);
        curr_play->set_text("please click one music!");
        curr_play->add_listener(EventType::MouseDown, onDown);
        curr_play->add_listener(EventType::MouseMove, onMove);
        curr_play->add_listener(EventType::Click, [this](Event<Node<Component>>* e)->bool {
            this->camera_reset();
            return true;
        });

        cxts.push_back(curr_play);
    }

    void init_btns()
    {
        auto switc = std::shared_ptr<Label>(new Label());
        switc->font = font2;
        switc->create();
        switc->color = wws::make_rgba(PREPARE_STRING("009E8EFF")).make<glm::vec4>();
        switc->size = 32;
        switc->get_comp<Transform>()->pos = glm::vec3(-1.72211f, -0.88618f, 0.4f);
        switc->set_text("change");
        switc->add_listener(EventType::MouseDown, onDown);
        switc->add_listener(EventType::MouseMove, onMove);
        switc->add_listener(EventType::Click, [this](Event<Node<Component>>* e)->bool {
            this->switch_to();
            return true;
        });

        cxts.push_back(switc);
    }

    void init_list_ui()
    {
        list_ui = std::shared_ptr<Sphere>(new Sphere(36, 31));
        list_ui->create();
        cxts.push_back(list_ui);


        list_ui->onAddOffset = [](const std::shared_ptr<Node<Component>>& c)->glm::vec3
        {
            auto p = dynamic_cast<Label*>(c.get());
            return glm::vec3(p->get_width() / -2.f, p->get_height() / 2.f, 0.f);
        };

        list_ui->get_comp<Transform>()->pos = glm::vec3(0.f, -0.385407, 0.32f);
        list_ui->get_comp<Transform>()->scale = glm::vec3(music_list_scale,music_list_scale,music_list_scale);

        pumper.setFillMusicFunc([this](const std::shared_ptr<std::vector<MMFile>>& list) {

            list_ani([this, list]() {
                list_ui->remove_all();
                int idx = 0;

                for (auto& s : *list)
                {
                    push_name(list_ui, s.get_name(), idx++);
                }
            });
        });
    }

    void init_flywheel()
    {
        flywheel = std::make_shared<Wheel>(6, 9.f);
        flywheel->pos.z = -9.f;
        flywheel->create();

        //auto v1 = std::make_shared<View1>();
        //v1->zl = 0.04f; 
        //v1->create();

        auto v2 = std::make_shared<View2>();
        v2->create();
        v2->line_width = 0.01f;
        flywheel->on_select = [this,v2](int i) 
        {
            switch (i) 
            {
            case 1:
                if (v2->get_comp<Transform>()->rotate.x == 0.f)
                {
                    std::weak_ptr<Transform> tra = v2->get_comp_ex<Transform>();
                    App::instance()->tween.to(tra, &Transform::rotate, &glm::vec3::x, 1000.f, 0.f, 0.3f, tween::Expo::easeInOut);
                }
                glLineWidth(1.2f);
                break;
            case 2:
               //if (v1->get_comp<Transform>()->rotate.x == 0.f)
               //{
               //    std::weak_ptr<Transform> tra = v1->get_comp_ex<Transform>();
               //    App::instance()->tween.to(tra, &Transform::rotate, &glm::vec3::x, 1000.f, 0.f, 0.47f, tween::Expo::easeInOut);
               //    App::instance()->tween.to(tra, &Transform::rotate, &glm::vec3::z, 1000.f, 0.f, 0.3f, tween::Expo::easeInOut);
               //}
               //glLineWidth(0.4f);
                break;
            }

        };

        flywheel->add(0, list_ui);
        flywheel->add(1, v2);
        //flywheel->add(2, v1);

        //cxts.push_back(v1);
        cxts.push_back(v2);
        fft_vs.push_back(v2);
        //fft_vs.push_back(v1);
    }

    void check_stop()
    {
        if (!player.isOff() && player.getActive() == BASS_ACTIVE_STOPPED && pumper.getMode() != Pumper::NONE)
        {
            pumper.pump();
        }
    }

    void switch_to()
    {
        int i = flywheel->get_curr() + 1 < flywheel->count() ? flywheel->get_curr() + 1 : 0;
        flywheel->tween_to(i, 1000.f);
    }

    void draw() override
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        update_mat(cxts);

        for (auto& p : cxts)
            p->draw();

        update();
        update_matrix();
        App::instance()->exec.do_loop();
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

        if (int i = flywheel->get_curr();i > 0)
        //for(int i = 1;i < flywheel->count();++i)
        {
            float* data = fft_ptr.get();
            size_t len = player.getData( data, fft_vs[i - 1]->fft_data_length()); 
            fft_vs[i - 1]->on_update(data, map_val(FFTView::FFT_VAL_TYPE(), fft_vs[i - 1]->fft_data_length()));
        }
        check_stop();
        
        if(player.is_playing())
        {
            double pos = player.getSeconds();
            lrc_mgr.playing(pos);
        }
        //dbg(pos);
    }

    ~Demo1() {

    }

    void onMouseButton(int btn,int action,int mode,double x,double y) override
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
    std::shared_ptr<Wheel> flywheel;
    std::string font = "fonts/SHOWG.TTF";
    std::string font2 = "fonts/happy.ttf";
    MusicPlayer player;
    Pumper pumper;
    std::vector<std::shared_ptr<FFTView>> fft_vs;
    std::function< bool(Event<Node<Component>>*) > onDown;
    std::function< bool(Event<Node<Component>>*) > onMove;
    std::unique_ptr<float[]> fft_ptr;
    float music_list_scale = 1.02f;
    lrc::DefLrcMgr lrc_mgr;
};

#ifndef PF_ANDROID

int main(int argc,const char**args)
{
    LaunchArgs = std::make_pair(args, argc);

    fs::path root = wws::find_path(3, "res", true);
    ResMgrWithGlslPreProcess::create_instance(root);
    DefResMgr::create_instance(std::move(root));
    Demo1 d;
    if (d.initWindow(1800, 960, "music player"))
    {
        printf("init window failed\n");
        return -1;
    }
    int err = 0;
    if ((err = d.init()) != 0) return err;
    d.run();

    return 0;
}

#endif