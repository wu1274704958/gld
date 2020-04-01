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

#define AsteroidNum 4800

struct AutoRotate : public Component
{
    void update() override
    {
        auto trans = node.lock()->get_comp<Transform>();
        if(trans->rotate.y > glm::pi<float>() * 2.0)
            trans->rotate.y = 0.f;
        else
            trans->rotate.y += 0.0001f * FrameRate::get_ms(); 
    }
};

class Demo1 : public RenderDemoRotate {
public:
    Demo1() : view_pos("view_pos"), perspective("perspective"), world("world")
        {}
    int init() override
    {
        RenderDemoRotate::init();
        
        program = DefDataMgr::instance()->load<DataType::Program>("lighting_5/base_vs.glsl","lighting_5/base_fg.glsl");
        program->use();

        program->locat_uniforms("perspective", "world", "model", "diffuseTex", "ambient_strength",
            "specular_strength",
            "view_pos",
            "shininess","specularTex"
        );

        asteroid_p = DefDataMgr::instance()->load<DataType::Program>("instantiation/base_vs.glsl","instantiation/base_fg.glsl");
        asteroid_p->use();

        asteroid_p->locat_uniforms("perspective", "world", "model", "diffuseTex", "ambient_strength",
            "specular_strength",
            "view_pos",
            "shininess","specularTex"
        );


        view_pos.attach_program(program);
        perspective.attach_program(program);
        world.attach_program(program);

        //glEnable(GL_BLEND);
        //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        planet = DefDataMgr::instance()->load<DataType::Scene>("model/planet/planet.obj",LoadScene::default_args(),
            "lighting_5/base_vs.glsl","lighting_5/base_fg.glsl");

        asteroid = DefDataMgr::instance()->load<DataType::Scene>("model/rock/rock.obj",LoadScene::default_args(),
            "instantiation/base_vs.glsl","instantiation/base_fg.glsl");

        auto trans = planet->get_comp<Transform>();
        trans->scale = glm::vec3(8.42f,8.42f,8.42f);
        trans->pos.y = -7.6f;

        trans = asteroid->get_comp<Transform>();
        trans->setRotateZ(12.f);

        

        def::Mesh* mesh = nullptr;
        std::shared_ptr<Node<Component>> ptr = asteroid;
        while(!mesh)
        {
            mesh = ptr->get_comp<def::Mesh>();
            if(ptr->children_count() > 0)
                ptr = ptr->get_child(0);
        }
        if(mesh)
        {
            auto mesh_ins = def::MeshInstanced::create_with_mesh(mesh,AsteroidNum);
            ptr->remove_comp(mesh);
            ptr->add_comp(mesh_ins);

            auto data = create_asteroids(AsteroidNum,glm::vec3(0.f,0.f,0.f));
            auto mat = glm::mat4(1.0f);
            auto& vao = mesh_ins->vao;
            vao->bind_self();
            auto& buf = vao->create_one();
            buf.bind_data(data.get(),AsteroidNum,GL_STATIC_DRAW);
            
            buf.vertex_attrib_pointer<3,VAP_DATA<4,float,false>,VAP_DATA<4,float,false>,VAP_DATA<4,float,false>,VAP_DATA<4,float,false>>();

            vao->vertex_attr_div<3,1,1,1,1>();
            vao->unbind_self();
        }
        
        cxts.push_back(planet);
        cxts.push_back(asteroid);

        planet->add_comp(std::make_shared<AutoRotate>());
        asteroid->get_child(0)->add_comp(std::make_shared<AutoRotate>());

        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

        GLenum err = glGetError();
        dbg(err);

        light.init(GL_STATIC_DRAW);
        
        light->color = glm::vec3(1.f, 1.f, 1.f);
        light->dir = glm::vec3(-0.2f, -1.0f, -0.3f);

        light.sync(GL_MAP_WRITE_BIT| GL_MAP_INVALIDATE_BUFFER_BIT);

        glm::vec3 view_p = glm::vec3(0.0f, 0.0f, 0.0f);
        view_pos = glm::value_ptr(view_p);
        view_pos.attach_program(asteroid_p);
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
        
        spl->color = glm::vec3(0.f,0.f, 0.f);
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

    std::unique_ptr<glm::mat4[]> create_asteroids(int n,glm::vec3 off)
    {
        glm::mat4 *res = new glm::mat4[n];
        srand(::time(nullptr)); // 初始化随机种子    
        float radius = 28.0;
        float offsetx = 3.78f;
        float offsety = 0.4f;
        float offsetz = 3.6f;
        for(int i = 0; i < n; i++)
        {
            glm::mat4 model(1.0f);
            // 1. 位移：分布在半径为 'radius' 的圆形上，偏移的范围是 [-offset, offset]
            float angle = (float)i / (float)n * glm::pi<float>() * 2.f;
            float displacement = (rand() % (int)(2 * offsetx * 100)) / 100.0f - offsetx;
            float x = sin(angle) * radius + displacement;
            displacement = (rand() % (int)(2 * offsety * 100)) / 100.0f - offsety;
            float y = displacement ;//* 0.4f; // 让行星带的高度比x和z的宽度要小
            displacement = (rand() % (int)(2 * offsetz * 100)) / 100.0f - offsetz;
            float z = cos(angle) * radius + displacement;
            model = glm::translate(model, glm::vec3(x + off.x, y + off.y, z + off.z));

            // 2. 缩放：在 0.05 和 0.25f 之间缩放
            float scale = ((rand() % 20) / 100.0f + 0.05f) * 0.7f;
            model = glm::scale(model, glm::vec3(scale));

            // 3. 旋转：绕着一个（半）随机选择的旋转轴向量进行随机的旋转
            float rotAngle = (rand() % 360);
            model = glm::rotate(model, rotAngle, glm::vec3(0.4f, 0.6f, 0.8f));

            // 4. 添加到矩阵的数组中
            res[i] = model;
        }  
        return std::unique_ptr<glm::mat4[]>(res);
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

        asteroid_p->use();
        perspective.attach_program(asteroid_p);
        world.attach_program(asteroid_p);
      
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

        world = glm::translate(*world, glm::vec3(0.0f, 0.0f, -50.0f));
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
    std::shared_ptr<Program> program,asteroid_p;
    UniformBuf<0,DictLight> light;
    Uniform<UT::Vec3> view_pos;
    GlmUniform<UT::Matrix4> perspective;
    GlmUniform<UT::Matrix4> world;
    std::vector<std::shared_ptr< gld::Node<gld::Component>>> cxts;
    UniformBuf<1,PointLights> pl;
    UniformBuf<2,SpotLight> spl;
    std::shared_ptr<Node<Component>> planet,asteroid;
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