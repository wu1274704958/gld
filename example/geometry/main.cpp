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
#include <cube_texture.hpp>
#include <stack>

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
    Demo1() : view_pos("view_pos"), perspective("perspective"), world("world") ,time_("time")
        {}
    int init() override
    {
        RenderDemoRotate::init();
        
        program = DefDataMgr::instance()->load<DataType::ProgramWithGeometry>("geometry/base_vs.glsl","geometry/base_fg.glsl","geometry/explode.glsl");
        program->use();

        program->locat_uniforms("perspective", "world", "model", "diffuseTex", "ambient_strength",
            "specular_strength",
            "view_pos",
            "shininess","specularTex","time"
        );

        GLenum err = glGetError();
        dbg(err);
        view_pos.attach_program(program);
        perspective.attach_program(program);
        world.attach_program(program);
        time_.attach_program(program);

        float skyboxVertices[] = {
            // positions          
            -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,

            -1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,

             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,

            -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,

            -1.0f,  1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,

            -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f,  1.0f
        };

        auto cube_tex = DefDataMgr::instance()->load<DataType::TextureCube>("textures/skybox","jpg",0);
        cube_tex->bind();
        cube_tex->generate_mipmap();
        cube_tex->set_paramter<TexOption::MIN_FILTER,TexOpVal::LINEAR>();
        cube_tex->set_paramter<TexOption::MAG_FILTER,TexOpVal::LINEAR>();
        cube_tex->set_paramter<TexOption::WRAP_R,TexOpVal::CLAMP_TO_EDGE>();
        cube_tex->set_paramter<TexOption::WRAP_S,TexOpVal::CLAMP_TO_EDGE>();
        cube_tex->set_paramter<TexOption::WRAP_T,TexOpVal::CLAMP_TO_EDGE>();

        auto skybox_vao = std::make_shared<gld::VertexArr>();
        skybox_vao->create();
        skybox_vao->create_arr<gld::ArrayBufferType::VERTEX>();
        skybox_vao->bind();
        skybox_vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(skyboxVertices,GL_STATIC_DRAW);
        skybox_vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
         gld::VAP_DATA<3,float,false>>();
        skybox_vao->unbind();

        auto skybox_mesh = std::shared_ptr<def::Mesh>(new def::Mesh(0,wws::arrLen(skyboxVertices)/3,skybox_vao ));

        skybox = std::make_shared<Node<Component>>();
        skybox->add_comp<def::Mesh>(skybox_mesh);
        skybox->add_comp<def::Skybox>(std::shared_ptr<def::Skybox>(new def::Skybox(cube_tex)));
        skybox->add_comp<Render>(std::shared_ptr<Render>(new Render("base/skybox_vs.glsl","base/skybox_fg.glsl")));

        //glEnable(GL_BLEND);
        //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        time_ = 0.f;
        time_.sync();

        auto node = DefDataMgr::instance()->load<DataType::SceneWithGeometry>("model/nanosuit/nanosuit.obj",LoadScene::default_args(),
            "geometry/base_vs.glsl","geometry/base_fg.glsl","geometry/explode.glsl");

        auto skybox_p = DefDataMgr::instance()->load<DataType::Program>("base/skybox_vs.glsl","base/skybox_fg.glsl");

        std::stack<std::shared_ptr<gld::Node<gld::Component>>> st;
        st.push(node);
        while (!st.empty())
        {
            auto ptr = st.top();
            st.pop();
            for(uint64_t i = 0;i < ptr->children_count();++i)
            { 
                auto mater = ptr->get_child(i)->get_comp<def::Material>();
                if(mater)
                    mater->uambient_strength = 0.36f;
                st.push(ptr->get_child(i));
            }
        }
       

        skybox_p->locat_uniforms("perspective", "world", "skybox");

        auto trans = node->get_comp<Transform>();
        trans->scale = glm::vec3(0.2f,0.2f,0.2f);
        trans->pos.y = -1.6f;
        
        cxts.push_back(node);

        node->add_comp(std::make_shared<AutoRotate>());

        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

        err = glGetError();
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
        skybox->init();
        update_matrix();
        return 0;
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
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        program->use();
        perspective.attach_program(program);
        world.attach_program(program);
        perspective.sync();
        world.sync();

        auto sky_pro = skybox->get_comp<Render>()->get();
        sky_pro->use();

        perspective.attach_program(sky_pro);
        world.attach_program(sky_pro);
        perspective.sync();
        world.sync();

        for (auto& p : cxts)
            p->draw();

        glDepthFunc(GL_LEQUAL);
        skybox->draw();
        glDepthFunc(GL_LESS);

        program->use();

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

        skybox->update();

        time_ = time_ + 0.01;
        time_.sync();
    }

    ~Demo1() {

    }

    void onWindowResize(int w, int h) override
    {
        glViewport(0, 0, w, h);
    }
private:
    std::shared_ptr<Program> program;
    UniformBuf<0,DictLight> light;
    Uniform<UT::Vec3> view_pos;
    GlmUniform<UT::Matrix4> perspective;
    GlmUniform<UT::Matrix4> world;
    std::vector<std::shared_ptr< gld::Node<gld::Component>>> cxts;
    UniformBuf<1,PointLights> pl;
    UniformBuf<2,SpotLight> spl;
    GlmUniform<UT::Float> time_;
    std::shared_ptr<Node<Component>> skybox;
    float pl_angle = 0.0f;
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