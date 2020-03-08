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
#include "../lighting_6/model.hpp"
#include <uniform.hpp>
#include "../lighting_6/light.hpp"
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

struct AutoRotate : public Component
{
    void update() override
    {
        auto trans = node.lock()->get_comp<Transform>();
        if(trans->rotate.y > glm::pi<float>() * 2.0)
            trans->rotate.y = 0.f;
        else
            trans->rotate.y += 0.001f * FrameRate::get_ms(); 
    }
};

class Demo1 : public RenderDemoRotate {
public:
    Demo1() : view_pos("view_pos"), perspective("perspective"), world("world")
        {}
    int init() override
    {
        RenderDemoRotate::init();
        
        program = DefDataMgr::instance()->load<DataType::Program>("lighting_6/base_vs.glsl","lighting_6/base_fg.glsl");
        program->use();

        program->locat_uniforms("perspective", "world", "model", "diffuseTex", "ambient_strength",
            "specular_strength",
            "view_pos",
            "shininess","specularTex"
        );

        base_p = DefDataMgr::instance()->load<DataType::Program>("base/base_vs.glsl","base/base_fg.glsl");
        base_p->use();

        base_p->locat_uniforms("perspective", "world", "model","fill_color");

        Uniform<UT::Vec3> fill_color("fill_color",base_p);
        
        glm::vec3 f_color = glm::vec3(0.04f, 0.28f, 0.26f);
        fill_color = glm::value_ptr(f_color);

        view_pos.attach_program(program);
        perspective.attach_program(program);
        world.attach_program(program);

        //glEnable(GL_BLEND);
        //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        program->use();
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_ALWAYS,1,0xFF);
        glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);

        auto node = DefDataMgr::instance()->load<DataType::Scene>("model/nanosuit/nanosuit.obj",LoadScene::default_args(),
            "lighting_6/base_vs.glsl","lighting_6/base_fg.glsl");

        auto trans = node->get_comp<Transform>();
        trans->scale = glm::vec3(0.2f,0.2f,0.2f);
        trans->pos.y = -1.6f;
        node->add_comp(std::make_shared<AutoRotate>());

        auto base_node = DefDataMgr::instance()->load<DataType::SceneNoMaterial>("model/nanosuit/nanosuit.obj",LoadScene::default_args(),
            "base/base_vs.glsl","base/base_fg.glsl");

        trans = base_node->get_comp<Transform>();
        trans->scale = glm::vec3(0.205f,0.205f,0.205f);
        trans->pos.y = -1.64f;
        trans->pos.z = 1.49012e-08f;
        base_node->add_comp(std::make_shared<AutoRotate>());
        
        cxts.push_back(node);
        cxts.push_back(base_node);
       

        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

        GLenum err = glGetError();
        dbg(err);

        light.init(GL_STATIC_DRAW);
        
        light->color = glm::vec3(1.f, 1.f, 1.f);
        light->dir = glm::vec3(-0.2f, -1.0f, -0.3f);

        light.sync(GL_MAP_WRITE_BIT| GL_MAP_INVALIDATE_BUFFER_BIT);

        glm::vec3 view_p = glm::vec3(0.0f, 0.0f, 0.0f);
        view_pos = glm::value_ptr(view_p);

        pl.init(GL_STATIC_DRAW);
        
        pl->pls[0].pos = glm::vec3(1.f, -2.f, 0.f);
        
        pl->pls[0].color = glm::vec3(1.f,0.f, 0.f);
        pl->pls[0].constant = 1.0f;
        pl->pls[0].linear = 0.09f;
        pl->pls[0].quadratic = 0.032f;
        pl->len = 3;

        pl->pls[1] = pl->pls[0];
        pl->pls[2] = pl->pls[0];

        pl->pls[1].pos = glm::vec3(1.f, -2.f, 2.f);
        pl->pls[1].color = glm::vec3(0.f, 1.f, 0.f);

        pl->pls[2].pos = glm::vec3(-1.f, -2.f, -2.f);
        pl->pls[2].color = glm::vec3(0.f, 0.0f, 1.0f);
        
        pl.sync(GL_MAP_WRITE_BIT|GL_MAP_INVALIDATE_BUFFER_BIT);

        spl.init(GL_STATIC_DRAW);

        spl->pos = glm::vec3(0.f, 0.f, 0.f);
        
        spl->color = glm::vec3(0.f,0.f, 1.f);
        spl->constant = 1.0f;
        spl->linear = 0.045f;
        spl->quadratic = 0.0075f;
        spl->dir = glm::vec3(0.0f,0.0f,-1.0f);
        spl->cut_off = glm::cos(glm::radians(12.0f));
        spl->outer_cut_off = glm::cos(glm::radians(13.5f));

        spl.sync(GL_MAP_WRITE_BIT|GL_MAP_INVALIDATE_BUFFER_BIT);

        for (auto& p : cxts)
            p->init();
        update_matrix();
        return 0;
    }

    void onMouseButton(int btn,int action,int mode) override
    {
        //dbg::log << btn << " " << action << " " << mode << dbg::endl;
        if(action == GLFW_RELEASE)
        { 
            switch (btn)
            {
            case 2:
                adjust_z = !adjust_z;
                break;
            case 4://up
            {
                adjust(-0.01f);
                break;            
            }
            case 3://down
            {
                adjust(0.01f);
                break;
            }
            }
            
        }
        RenderDemoRotate::onMouseButton(btn,action,mode);
    }

    void adjust(float off)
    {
        if(adjust_z)
        {
            auto trans = cxts[1]->get_comp<Transform>();
            trans->pos.z += off;
            dbg::log << "pos z " << trans->pos.z << dbg::endl;
        }else{
            auto trans = cxts[1]->get_comp<Transform>();
            trans->pos.y += off;
            dbg::log << "pos y " << trans->pos.y << dbg::endl;
        }
    }

    void loadModel()
    {
        auto res = ResMgrWithGlslPreProcess::instance()->load<ResType::model>("model/nanosuit/nanosuit.obj");
        if(res)
        {
            dbg::log << "load success " << res->GetScene()->mNumMeshes << dbg::endl;
        }else
            dbg::log << "load model failed" << dbg::endl;
    }

    float rd_0_1()
    {
        std::random_device r;
        std::default_random_engine e1(r());
        std::uniform_int_distribution<int> uniform_dist(0, 1000000);
        return static_cast<float>(uniform_dist(e1)) / 100000.f;
    }

    void draw() override
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        program->use();
        perspective.attach_program(program);
        world.attach_program(program);
        perspective.sync();
        world.sync();

        base_p->use();
        perspective.attach_program(base_p);
        world.attach_program(base_p);
        perspective.sync();
        world.sync();

        // for (auto& p : cxts)
        //     p->draw();

        glStencilMask(0xff);
        glStencilFunc(GL_ALWAYS,1,0xff);

        cxts[0]->draw();

        glStencilFunc(GL_NOTEQUAL,1,0xff);
        
        glStencilMask(0x0);

        cxts[1]->draw();
        glStencilMask(0xff);

        update();
        update_matrix();

    }

    void update_matrix()
    {
        perspective = glm::perspective(glm::radians(60.f), ((float)width / (float)height), 0.1f, 256.0f);
        world = glm::mat4(1.0f);

        world = glm::translate(*world, glm::vec3(0.0f, 0.0f, -3.0f));
        world = glm::rotate(*world, glm::radians(rotate.x), glm::vec3(1.f, 0.f, 0.f));
        world = glm::rotate(*world, glm::radians(rotate.y), glm::vec3(0.f, 1.f, 0.f));
        world = glm::rotate(*world, glm::radians(rotate.z), glm::vec3(0.f, 0.f, 1.f));
    }

    void update()
    {
        for (auto& p : cxts)
            p->update();
        
        glm::vec3 pl_pos = glm::vec3(1.f, -2.f, 0.f);
        pl_pos = glm::rotateZ(pl_pos,pl_angle);
        pl->pls[0].pos = pl_pos;
        pl.sync(GL_MAP_WRITE_BIT);
        if(pl_angle >= glm::pi<float>() * 2.0f) pl_angle = 0.0f; else pl_angle += 0.02f;
    }

    ~Demo1() {

    }

    void onWindowResize(int w, int h) override
    {
        glViewport(0, 0, w, h);
    }
private:
    std::shared_ptr<Program> program,base_p;
    UniformBuf<0,DictLight> light;
    Uniform<UT::Vec3> view_pos;
    GlmUniform<UT::Matrix4> perspective;
    GlmUniform<UT::Matrix4> world;
    std::vector<std::shared_ptr< gld::Node<gld::Component>>> cxts;
    UniformBuf<1,PointLights> pl;
    UniformBuf<2,SpotLight> spl;
    float pl_angle = 0.0f;
    bool adjust_z = true;
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