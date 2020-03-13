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
#include <frame_buffer.hpp>

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

struct ScreenMat : public Component
{
    ScreenMat(std::shared_ptr<Texture<TexType::D2>> tex) :
        utex("screenTexture"),
        tex(std::move(tex))
    {
        utex = 0;
    }
    bool init() override
    {
        auto n_ptr = get_node();
        auto render = n_ptr->get_comp<Render>();
        utex.attach_program(render->get());
        return true;
    }
    void draw() override
    {
        if(tex)
            tex->active<ActiveTexId::_0>();

        utex.sync();
    }
        
    GlmUniform<UT::Sampler2D> utex;
    std::shared_ptr<Texture<TexType::D2>> tex;
};

#define VER_PATH "lighting_5/base_vs.glsl"
#define FRAG_PATH "lighting_5/base_fg.glsl"

class Demo1 : public RenderDemoRotate {
public:
    Demo1() : view_pos("view_pos"), perspective("perspective"), world("world")
        {}
    int init() override
    {
        RenderDemoRotate::init();
        
        program = DefDataMgr::instance()->load<DataType::Program>(VER_PATH,FRAG_PATH);
        program->use();

        program->locat_uniforms("perspective", "world", "model", "diffuseTex", "ambient_strength",
            "specular_strength",
            "view_pos",
            "shininess","specularTex"
        );

        auto p = DefDataMgr::instance()->load<DataType::Program>("framebuffer/vert.glsl","framebuffer/frag.glsl");
        p->use();
        p->locat_uniforms("screenTexture");

        view_pos.attach_program(program);
        perspective.attach_program(program);
        world.attach_program(program);

        //glEnable(GL_BLEND);
        //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glDisable(GL_CULL_FACE);
        //glCullFace(GL_BACK);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        float cubeVertices[] = {
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

        float planeVertices[] = {
            // positions          //normal            // texture Coords 
             5.0f, -0.5f,  5.0f,  0.0f,  1.0f,  0.0f,  2.0f, 0.0f,
            -5.0f, -0.5f,  5.0f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f,
            -5.0f, -0.5f, -5.0f,  0.0f,  1.0f,  0.0f,  0.0f, 2.0f,
  
             5.0f, -0.5f,  5.0f,  0.0f,  1.0f,  0.0f,  2.0f, 0.0f,
            -5.0f, -0.5f, -5.0f,  0.0f,  1.0f,  0.0f,  0.0f, 2.0f,
             5.0f, -0.5f, -5.0f,  0.0f,  1.0f,  0.0f,  2.0f, 2.0f
        };

        float quadVertices[] = { // vertex attributes for a quad that fills the entire screen in Normalized Device Coordinates.
        // positions   // texCoords
            -1.0f,  1.0f,  0.0f, 1.0f,
            -1.0f, -1.0f,  0.0f, 0.0f,
             1.0f, -1.0f,  1.0f, 0.0f,

            -1.0f,  1.0f,  0.0f, 1.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 1.0f
        };

        auto cube_vao = std::make_shared<gld::VertexArr>();
        cube_vao->create();
        cube_vao->create_arr<gld::ArrayBufferType::VERTEX>();
        cube_vao->bind();
        cube_vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(cubeVertices,GL_STATIC_DRAW);
        cube_vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
         gld::VAP_DATA<3,float,false>,
         gld::VAP_DATA<3,float,false>,
         gld::VAP_DATA<2,float,false>>();
        cube_vao->unbind();

        auto plane_vao = std::make_shared<gld::VertexArr>();
        plane_vao->create();
        plane_vao->create_arr<gld::ArrayBufferType::VERTEX>();
        plane_vao->bind();
        plane_vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(planeVertices,GL_STATIC_DRAW);
        plane_vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
         gld::VAP_DATA<3,float,false>,
         gld::VAP_DATA<3,float,false>,
         gld::VAP_DATA<2,float,false>>();
        plane_vao->unbind();

        auto quad_vao = std::make_shared<gld::VertexArr>();
        quad_vao->create();
        quad_vao->create_arr<gld::ArrayBufferType::VERTEX>();
        quad_vao->bind();
        quad_vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(quadVertices,GL_STATIC_DRAW);
        quad_vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
         gld::VAP_DATA<2,float,false>,
         gld::VAP_DATA<2,float,false>>();
        quad_vao->unbind();

        auto cube_mesh = std::shared_ptr<def::Mesh>(new def::Mesh(0,wws::arrLen(cubeVertices)/8,cube_vao ));
        auto plane_mesh = std::shared_ptr<def::Mesh>(new def::Mesh(0,wws::arrLen(planeVertices)/8,plane_vao ));
        auto quad_mesh = std::shared_ptr<def::Mesh>(new def::Mesh(0,wws::arrLen(quadVertices)/4,quad_vao ));

        auto dif_cube = DefDataMgr::instance()->load<DataType::Texture2D>("textures/container.jpg",0);
        auto dif_plane = DefDataMgr::instance()->load<DataType::Texture2D>("textures/metal.png",0);

        auto cube_mat = std::shared_ptr<def::Material>(new def::Material(dif_cube,nullptr));
        auto plane_mat = std::shared_ptr<def::Material>(new def::Material(dif_plane,nullptr));

        auto cube = std::make_shared<Node<Component>>();
        cube->add_comp<Transform>(std::make_shared<Transform>());
        cube->add_comp<def::Mesh>(cube_mesh);
        cube->add_comp<def::Material>(cube_mat);
        cube->add_comp<Render>(std::shared_ptr<Render>(new Render(VER_PATH,FRAG_PATH)));

        auto cube2 = std::make_shared<Node<Component>>();
        cube2->add_comp<Transform>(std::make_shared<Transform>());
        cube2->add_comp<def::Mesh>(cube_mesh);
        cube2->add_comp<def::Material>(cube_mat);
        cube2->add_comp<Render>(std::shared_ptr<Render>(new Render(VER_PATH,FRAG_PATH)));

        auto plane = std::make_shared<Node<Component>>();
        plane->add_comp<Transform>(std::make_shared<Transform>());
        plane->add_comp<def::Mesh>(plane_mesh);
        plane->add_comp<def::Material>(plane_mat);
        plane->add_comp<Render>(std::shared_ptr<Render>(new Render(VER_PATH,FRAG_PATH)));

        cube->get_comp<Transform>()->pos = glm::vec3(-1.0f, 0.0f, -1.0f);
        cube2->get_comp<Transform>()->pos = glm::vec3(2.0f, 0.0f, 0.0f);

        cxts.push_back(cube);
        cxts.push_back(cube2);
        cxts.push_back(plane);

        auto screen_tex = std::make_shared<Texture<TexType::D2>>();
        screen_tex->create();
        screen_tex->bind();
        screen_tex->tex_image(0,GL_RGB,0,GL_RGB,(unsigned char *)nullptr,width,height);
        screen_tex->set_paramter<TexOption::MIN_FILTER,TexOpVal::LINEAR>();
        screen_tex->set_paramter<TexOption::MAG_FILTER,TexOpVal::LINEAR>();
        screen_tex->set_paramter<TexOption::WRAP_R,TexOpVal::CLAMP_TO_EDGE>();
        screen_tex->set_paramter<TexOption::WRAP_S,TexOpVal::CLAMP_TO_EDGE>();

        auto quad_mat = std::shared_ptr<ScreenMat>(new ScreenMat(screen_tex));

        screen  = std::make_shared<Node<Component>>();
        screen->add_comp<def::Mesh>(quad_mesh);
        screen->add_comp<ScreenMat>(quad_mat);
        screen->add_comp<Render>(std::shared_ptr<Render>(new Render("framebuffer/vert.glsl","framebuffer/frag.glsl")));


        fb.create();
        fb.bind();
        fb.attach_texture(*screen_tex.get(),GL_COLOR_ATTACHMENT0,0);
        
        rb.create();
        rb.bind();
        rb.storage(GL_DEPTH24_STENCIL8,width,height);
        
        fb.attach_buffer(GL_DEPTH_STENCIL_ATTACHMENT,rb);

        dbg(fb.check_status() == GL_FRAMEBUFFER_COMPLETE);

        fb.unbind();


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

        screen->init();
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
        fb.bind();
        program->use();
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f); 
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      
        perspective.sync();
        world.sync();

        for (auto& p : cxts)
            p->draw();

        program->use();

        update();
        update_matrix();

        fb.unbind();
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f); 
        glDisable(GL_DEPTH_TEST);
        glClear(GL_COLOR_BUFFER_BIT);

        screen->draw();

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

        screen->update();
        
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
    UniformBuf<0,DictLight> light;
    Uniform<UT::Vec3> view_pos;
    GlmUniform<UT::Matrix4> perspective;
    GlmUniform<UT::Matrix4> world;
    std::vector<std::shared_ptr< gld::Node<gld::Component>>> cxts;
    UniformBuf<1,PointLights> pl;
    UniformBuf<2,SpotLight> spl;
    float pl_angle = 0.0f;
    std::shared_ptr<Node<Component>> screen;
    FrameBuffer fb;
    RenderBuffer rb;
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