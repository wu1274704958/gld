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

using namespace gld;
namespace fs = std::filesystem;

using  namespace dbg::literal;

class Demo1 : public RenderDemoRotate {
public:
    Demo1() : view_pos("view_pos"), perspective("perspective"), world("world")
        {}
    int init() override
    {
        RenderDemoRotate::init();
        
        program = DefDataMgr::instance()->load<DataType::Program>("lighting_6/base_vs.glsl","lighting_6/base_fg.glsl");

        diffuseTex = DefDataMgr::instance()->load<DataType::Texture2D>("lighting_2/container2.png",0);
        specularTex = DefDataMgr::instance()->load<DataType::Texture2D>("lighting_3/container2_specular.png",0);

        dbg::log << "ty_name "<< typeid(int).name() << " " << diffuseTex->good() << " "<< specularTex->good()  << dbg::endl;

        view_pos.attach_program(program);
        perspective.attach_program(program);
        world.attach_program(program);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);

        loadModel();

        program->use();

        program->locat_uniforms("perspective", "world", "model", "diffuseTex", "ambient_strength",
            "specular_strength",
            "view_pos",
            "shininess","specularTex"
            );

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        if(diffuseTex && specularTex)
        {
            diffuseTex->bind();
            diffuseTex->generate_mipmap();
            diffuseTex->set_paramter<TexOption::WRAP_S,TexOpVal::REPEAT>();
            diffuseTex->set_paramter<TexOption::WRAP_T,TexOpVal::REPEAT>();

            diffuseTex->set_paramter<TexOption::MIN_FILTER,TexOpVal::LINEAR_MIPMAP_LINEAR>();
            diffuseTex->set_paramter<TexOption::MAG_FILTER,TexOpVal::LINEAR>();

            specularTex->bind();
            specularTex->generate_mipmap();
            specularTex->set_paramter<TexOption::WRAP_S,TexOpVal::REPEAT>();
            specularTex->set_paramter<TexOption::WRAP_T,TexOpVal::REPEAT>();

            specularTex->set_paramter<TexOption::MIN_FILTER,TexOpVal::LINEAR_MIPMAP_LINEAR>();
            specularTex->set_paramter<TexOption::MAG_FILTER,TexOpVal::LINEAR>();
        }else{
            dbg("load texture failed!!");
            return -1;
        }

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

        float vertices[] = {
            // positions          // normals           // texture coords
         -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f,
          0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f,  0.0f,
          0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f,  1.0f,
          0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  1.0f,  1.0f,
         -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f,  1.0f,
         -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,  0.0f,  0.0f,

         -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,
          0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f,  0.0f,
          0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f,  1.0f,
          0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f,  1.0f,
         -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  1.0f,
         -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f,  0.0f,

         -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f,  0.0f,
         -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  1.0f,  1.0f,
         -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
         -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
         -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  0.0f,  0.0f,
         -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,  1.0f,  0.0f,

          0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,
          0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  1.0f,
          0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
          0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,  0.0f,  1.0f,
          0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  0.0f,  0.0f,
          0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f,  0.0f,

         -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f,  1.0f,
          0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  1.0f,  1.0f,
          0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f,  0.0f,
          0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  1.0f,  0.0f,
         -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,  0.0f,  0.0f,
         -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,  0.0f,  1.0f,

         -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,
          0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  1.0f,  1.0f,
          0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f,  0.0f,
          0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f,  0.0f,
         -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f,  0.0f,
         -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f
        };
        
        va1.create();
        va1.create_arr<ArrayBufferType::VERTEX>();

        va1.bind();
        va1.buffs().get<ArrayBufferType::VERTEX>().bind_data(vertices, GL_STATIC_DRAW);
        va1.buffs().get<ArrayBufferType::VERTEX>().vertex_attrib_pointer<
            VAP_DATA<3, float, false>, 
            VAP_DATA<3, float, false>,
            VAP_DATA<2,float,false>>();
        va1.unbind();

        glm::vec3 cubePositions[] = {
            glm::vec3( 0.0f,  0.0f,  0.0f),
            glm::vec3( 2.0f,  5.0f, -15.0f),
            glm::vec3(-1.5f, -2.2f, -2.5f),
            glm::vec3(-3.8f, -2.0f, -12.3f),
            glm::vec3( 2.4f, -0.4f, -3.5f),
            glm::vec3(-1.7f,  3.0f, -7.5f),
            glm::vec3( 1.3f, -2.0f, -2.5f),
            glm::vec3( 1.5f,  2.0f, -2.5f),
            glm::vec3( 1.5f,  0.2f, -1.5f),
            glm::vec3(-1.3f,  1.0f, -1.5f)
        };
        for(int i = 0;i < wws::arrLen(cubePositions);++i)
        {
            cxts.push_back(std::unique_ptr<Model>(new Model(program, va1, 12,diffuseTex,specularTex)));

            cxts[i]->scale = glm::vec3(1.f, 1.f, 1.f);
            auto ptr = dynamic_cast<Model*>(cxts[i].get());
            ptr->material.shininess = 32.f;
            ptr->material.specular_strength = 0.7f;
            ptr->material.ambient_strength = 0.1f;
            ptr->material.diffuseTex = 0;
            ptr->material.specularTex = 1;
            ptr->pos = cubePositions[i];

            ptr->rotate = glm::vec3(rd_0_1(),rd_0_1(),rd_0_1());
        }

        update_matrix();

        for (auto& p : cxts)
            p->init();

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

        perspective.sync();
        world.sync();

        for (auto& p : cxts)
            p->draw();

        update();
        update_matrix();

        program->unuse();
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
    std::shared_ptr<Program> program;
    VertexArr va1, va2;
    UniformBuf<0,DictLight> light;
    Uniform<UT::Vec3> view_pos;
    GlmUniform<UT::Matrix4> perspective;
    GlmUniform<UT::Matrix4> world;
    std::vector<std::unique_ptr<Drawable>> cxts;
    std::shared_ptr<Texture<TexType::D2>> diffuseTex,specularTex;
    UniformBuf<1,PointLights> pl;
    UniformBuf<2,SpotLight> spl;
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