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
    return glm::vec3(rd_0_1(), rd_0_1(), rd_0_1());
}

#define SCALE 0.001f

class Demo1 : public RenderDemoRotate {
public:
    Demo1() : perspective("perspective"), world("world"), fill_color("fill_color"), view_pos("view_pos")
    {}
    int init() override
    {
        RenderDemoRotate::init();

        img_idx = img_count;

        glEnable(GL_DEPTH_TEST);

        rotate.z = 5.4f;

        program = DefDataMgr::instance()->load<DataType::Program>("base/curve_vs.glsl", "base/curve_fg.glsl");
        program->use();

        program->locat_uniforms("perspective", "world", "model", "diffuseTex", "ambient_strength",
            "specular_strength",
            "view_pos",
            "shininess", "specularTex");

        perspective.attach_program(program);
        world.attach_program(program);
        view_pos.attach_program(program);

        glm::vec3 vp(0.f, 0.f, 0.f);
        view_pos = glm::value_ptr(vp);

        light.init(GL_STATIC_DRAW);

        light->color = glm::vec3(1.f, 1.f, 1.f);
        light->dir = glm::vec3(-0.2f, -1.0f, -0.3f);

        light.sync(GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);

        auto mesh = curved_surface(0.8f, 0.8f, 0.10f, 0.08f, 32, true);

        mesh->mode = GL_TRIANGLE_STRIP;

        auto spec = DefDataMgr::instance()->load<DataType::Texture2D>("textures/white.png",0);

        for (int i = 0; i < img_count; ++i)
        {
            std::string s("99/");
            s += wws::to_string(i + 1);
            s += ".jpg";
            images.push_back( DefDataMgr::instance()->load<DataType::Texture2D>(s, 0) );
        }

        

        auto cir = gen::circle(0.f, 7.4f, count);
        float m = glm::pi<float>() * 2.f;
        for (int i = 0; i < cir.size(); ++i)
        {
            
            auto trans =  std::make_shared<Transform>();
            trans->pos = cir[i];
            trans->rotate.y = (float)i / (float)count * m;
            auto& dif = images[get_image_idx()];
            trans->scale = glm::vec3( static_cast<float>(dif->measure.width) * SCALE,static_cast<float>(dif->measure.height) * SCALE, 1.f);
            auto material = std::shared_ptr<def::Material>(new def::Material(dif, spec));
            auto node = create(mesh,trans,material);
            cxts.push_back(node);
        }
       
        angle_ = m / static_cast<float>(count);
        //cxts.push_back(node);

        //glEnable(GL_FRAMEBUFFER_SRGB);

        for (auto& p : cxts)
            p->init();

        return 0;
    }

    int get_image_idx()
    {
        ++img_idx;
        if (img_idx >= img_count)
            img_idx = 0;
        return img_idx;
    }

    std::shared_ptr<Node<Component>> create(std::shared_ptr<def::Mesh> mesh, std::shared_ptr<Transform> trans,std::shared_ptr<def::Material> material)
    {
        auto node = std::make_shared<Node<Component>>();
        node->add_comp<Transform>(trans);
        node->add_comp<def::Mesh>(mesh);
        node->add_comp<Render>(std::make_shared<Render>("base/curve_vs.glsl", "base/curve_fg.glsl"));

        auto plane_mat = std::shared_ptr<def::Material>(material);

        plane_mat->uambient_strength = 0.03f;
        plane_mat->ushininess = 256.f;
        plane_mat->uspecular_strength = 0.5f;

        node->add_comp<def::Material>(plane_mat);

        return node;
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

        world = glm::translate(*world, pos);
        world = glm::rotate(*world, glm::radians(rotate.x), glm::vec3(1.f, 0.f, 0.f));
        world = glm::rotate(*world, glm::radians(rotate.z), glm::vec3(0.f, 0.f, 1.f));
        world = glm::rotate(*world, glm::radians(rotate.y), glm::vec3(0.f, 1.f, 0.f));
       
    }

    void update()
    {
        for (auto& p : cxts)
            p->update();
        
        r += FrameRate::get_ms() * r_zl; //rotate.y / angle_ - glm::floor(rotate.y / angle_);
        rotate.y += glm::sin(r) * 100.f;

        rr += glm::sin(r) * 100.f;
        if (rr >= 360.f / static_cast<float>(count) )
        {
            rr = 0.f;
            r = 0.f;
            rotate.y = rrr + 360.f / static_cast<float>(count);
            rrr = rotate.y;
        }


        if (rotate.y >= 360.f)
        {
            rotate.y = 0.f; rrr = 0.f;
        }
    }

    ~Demo1() {

    }

    void onMouseButton(int btn, int action, int mode,int x,int y) override
    {
        RenderDemoRotate::onMouseButton(btn, action, mode,x,y);
        if (btn == GLFW_MOUSE_BUTTON_2 && action == GLFW_PRESS)
        {
            pos.y -= 0.1f;
        }
        if (btn == GLFW_MOUSE_BUTTON_3 && action == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(m_window, 1);
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
    UniformBuf<0, DictLight> light;
    Uniform<UT::Vec3> view_pos;
    std::vector<std::shared_ptr<Texture<TexType::D2>>> images;
    int img_idx = 0;
    int count = 18;
    int img_count = 12; 
    float angle_;
    float r = 0.f;
    float r_zl = 0.000002f;//0.000405f;
    float rr = 0.f;
    float rrr = 0.f;
    bool ok = false;
    glm::vec3 pos = glm::vec3(0.f, 0.f, -10.56f);
};

#ifndef PF_ANDROID
int main()
{
    fs::path root = wws::find_path(3, "res", true);
    ResMgrWithGlslPreProcess::create_instance(root);
    DefResMgr::create_instance(std::move(root));

    Demo1 d;
    if (d.initWindow(1920, 1080, "Clock", []() {
        glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
        glfwWindowHint(GLFW_DECORATED, GL_FALSE);
    }))
    {
        printf("init window failed\n");
        return -1;
    }
    d.init();
    d.run();

    return 0;
}

#endif