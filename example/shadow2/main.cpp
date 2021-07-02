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

#define DEPTH_TEX_W 1024

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
        utex = 2;
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
            tex->active<ActiveTexId::_2>();

        utex.sync();
    }

    void after_draw() override
    {
        tex->unbind();
    }
        
    GlmUniform<UT::Sampler2D> utex;
    std::shared_ptr<Texture<TexType::D2>> tex;
};

#define VER_PATH "shadow/base_vs.glsl"
#define FRAG_PATH "shadow/base_fg.glsl"

#define Blinn_VER_PATH "blinn/base_vs.glsl"
#define Blinn_FRAG_PATH "blinn/base_fg.glsl"

#define Depth_VER_PATH "shadow_map/base_vs.glsl"
#define Depth_FRAG_PATH "shadow_map/base_fg.glsl"

class Demo1 : public RenderDemoRotate {
public:
    Demo1() : view_pos("view_pos"), perspective("perspective"), world("world") ,use_blinn("use_blinn"), lightSpaceMartix("lightSpaceMartix")
        {}
    int init() override
    {
        RenderDemoRotate::init();

        depth_p = DefDataMgr::instance()->load<DataType::Program>(Depth_VER_PATH,Depth_FRAG_PATH);
        depth_p->use();

        depth_p->locat_uniforms("perspective", "world", "model");
        
        program = DefDataMgr::instance()->load<DataType::Program>(VER_PATH,FRAG_PATH);
        program->use();

        program->locat_uniforms("perspective", "world", "model", "diffuseTex", "ambient_strength",
            "specular_strength",
            "view_pos",
            "shininess","specularTex" , "use_blinn","screenTexture","lightSpaceMartix"
        );

        blinn_p = DefDataMgr::instance()->load<DataType::Program>(Blinn_VER_PATH,Blinn_FRAG_PATH);
        blinn_p->use();

        blinn_p->locat_uniforms("perspective", "world", "model", "diffuseTex", "ambient_strength",
            "specular_strength",
            "view_pos",
            "shininess","specularTex" , "use_blinn"
        );

        glm::vec3 view_p = glm::vec3(0.0f, 0.0f, 0.0f);
        
        program->use();

        view_pos.attach_program(program);
        
        perspective.attach_program(program);
        world.attach_program(program);
        use_blinn.attach_program(program);
        lightSpaceMartix.attach_program(program);
        //test depth texture 
        use_blinn = 1;
        use_blinn.sync();

        view_pos = glm::value_ptr(view_p);

        blinn_p->use();
        use_blinn.attach_program(blinn_p);
        use_blinn.sync();

        view_pos.attach_program(blinn_p);
        view_pos = glm::value_ptr(view_p);
        program->use();

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
             5.0f, -0.5f,  5.0f,  0.0f,  1.0f,  0.0f,  8.0f, 0.0f,
            -5.0f, -0.5f,  5.0f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f,
            -5.0f, -0.5f, -5.0f,  0.0f,  1.0f,  0.0f,  0.0f, 8.0f,
  
             5.0f, -0.5f,  5.0f,  0.0f,  1.0f,  0.0f,  8.0f, 0.0f,
            -5.0f, -0.5f, -5.0f,  0.0f,  1.0f,  0.0f,  0.0f, 8.0f,
             5.0f, -0.5f, -5.0f,  0.0f,  1.0f,  0.0f,  8.0f, 8.0f
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
        float color[] = {1.f,1.f,1.f,1.f};

        
        dbg(glGetError());

        depth_tex = std::make_shared<Texture<TexType::D2>>();
        depth_tex->create();
        depth_tex->bind();
        depth_tex->tex_image(0,GL_DEPTH_COMPONENT,0,GL_DEPTH_COMPONENT,(float *)nullptr,DEPTH_TEX_W,DEPTH_TEX_W);
        depth_tex->set_paramter<TexOption::MIN_FILTER,TexOpVal::NEAREST>();
        depth_tex->set_paramter<TexOption::MAG_FILTER,TexOpVal::NEAREST>();
        depth_tex->set_paramter<TexOption::WRAP_T,TexOpVal::CLAMP_TO_BORDER>();
        depth_tex->set_paramter<TexOption::WRAP_S,TexOpVal::CLAMP_TO_BORDER>();
        depth_tex->set_paramter<TexOption::BORDER_COLOR>(color);
        depth_tex->unbind();

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

        auto depth_tex_comp = std::make_shared<ScreenMat>(depth_tex);

        auto dif_cube = DefDataMgr::instance()->load<DataType::Texture2D>("textures/container.jpg",0);
        auto dif_plane = DefDataMgr::instance()->load<DataType::Texture2D>("textures/wood.png",0);
        auto spec_plane = DefDataMgr::instance()->load<DataType::Texture2D>("textures/wood_spec.png",0);

        auto cube_mat = std::shared_ptr<def::Material>(new def::Material(dif_plane,nullptr));
        auto plane_mat = std::shared_ptr<def::Material>(new def::Material(dif_plane,spec_plane));

        plane_mat->ushininess = 128.f;
        plane_mat->uspecular_strength = 0.5f;
        plane_mat->uambient_strength = 0.02f;

        auto cube = std::make_shared<Node<Component>>();
        cube->add_comp<Transform>(std::make_shared<Transform>());
        cube->add_comp<def::Mesh>(cube_mesh);
        cube->add_comp<def::Material>(cube_mat);
        cube->add_comp<Render>(std::shared_ptr<Render>(new Render(Blinn_VER_PATH,Blinn_FRAG_PATH)));

        auto cube2 = std::make_shared<Node<Component>>();
        cube2->add_comp<Transform>(std::make_shared<Transform>());
        cube2->add_comp<def::Mesh>(cube_mesh);
        cube2->add_comp<def::Material>(cube_mat);
        cube2->add_comp<Render>(std::shared_ptr<Render>(new Render(Blinn_VER_PATH,Blinn_FRAG_PATH)));

        auto cube3 = std::make_shared<Node<Component>>();
        cube3->add_comp<Transform>(std::make_shared<Transform>());
        cube3->add_comp<def::Mesh>(cube_mesh);
        cube3->add_comp<def::Material>(cube_mat);
        cube3->add_comp<Render>(std::shared_ptr<Render>(new Render(Blinn_VER_PATH,Blinn_FRAG_PATH)));

        plane = std::make_shared<Node<Component>>();
        plane->add_comp<Transform>(std::make_shared<Transform>());
        plane->add_comp<def::Mesh>(plane_mesh);
        plane->add_comp<def::Material>(plane_mat);
        if(*use_blinn == 1)
            plane->add_comp<ScreenMat>(depth_tex_comp);
        else
            plane->add_comp<ScreenMat>(std::make_shared<ScreenMat>(dif_plane));
        plane->add_comp<Render>(std::shared_ptr<Render>(new Render(VER_PATH,FRAG_PATH)));

        plane->get_comp<Transform>()->scale = glm::vec3(4.f,1.f,4.f);
        plane->get_comp<Transform>()->pos.y = -1.6f;

        cube->get_comp<Transform>()->pos = glm::vec3(-1.0f, 0.0f, -1.0f);
        cube2->get_comp<Transform>()->pos = glm::vec3(2.0f, 0.0f, 0.0f);
        cube3->get_comp<Transform>()->pos = glm::vec3(2.0f, -1.6f, -2.0f);

        cube->get_comp<Transform>()->rotate = glm::vec3(rd_0_1(), rd_0_1(), rd_0_1());
        cube2->get_comp<Transform>()->rotate = glm::vec3(rd_0_1(), rd_0_1(), rd_0_1());


        cxts.push_back(cube);
        cxts.push_back(cube2);
        cxts.push_back(cube3);

        auto cube_d = std::make_shared<Node<Component>>();
        cube_d->add_comp<Transform>(std::make_shared<Transform>());
        cube_d->add_comp<def::Mesh>(cube_mesh);
        cube_d->add_comp<Render>(std::shared_ptr<Render>(new Render(Depth_VER_PATH,Depth_FRAG_PATH)));

        auto cube2_d = std::make_shared<Node<Component>>();
        cube2_d->add_comp<Transform>(std::make_shared<Transform>());
        cube2_d->add_comp<def::Mesh>(cube_mesh);
        cube2_d->add_comp<Render>(std::shared_ptr<Render>(new Render(Depth_VER_PATH,Depth_FRAG_PATH)));

        auto cube3_d = std::make_shared<Node<Component>>();
        cube3_d->add_comp<Transform>(std::make_shared<Transform>());
        cube3_d->add_comp<def::Mesh>(cube_mesh);
        cube3_d->add_comp<Render>(std::shared_ptr<Render>(new Render(Depth_VER_PATH,Depth_FRAG_PATH)));

        auto plane_d = std::make_shared<Node<Component>>();
        plane_d->add_comp<Transform>(std::make_shared<Transform>());
        plane_d->add_comp<def::Mesh>(plane_mesh);
        plane_d->add_comp<Render>(std::shared_ptr<Render>(new Render(Depth_VER_PATH,Depth_FRAG_PATH)));

        cube_d->get_comp<Transform>()->pos = cube->get_comp<Transform>()->pos; 
        cube2_d->get_comp<Transform>()->pos = cube2->get_comp<Transform>()->pos;
        cube3_d->get_comp<Transform>()->pos = cube3->get_comp<Transform>()->pos;

        cube_d->get_comp<Transform>()->rotate = cube->get_comp<Transform>()->rotate; 
        cube2_d->get_comp<Transform>()->rotate = cube2->get_comp<Transform>()->rotate;

        (*(plane_d->get_comp<Transform>())) = (*(plane->get_comp<Transform>()));

        depth_nodes.push_back(cube_d );
        depth_nodes.push_back(cube2_d);
        depth_nodes.push_back(cube3_d);
       // depth_nodes.push_back(plane_d);


        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

        GLenum err = glGetError();
        dbg(err);

        light.init(GL_STATIC_DRAW);
        
        light->color = glm::vec3(1.f, 1.f, 1.f);
#ifdef PF_ANDROID
        light->a = 0.1f;
        light->dir = glm::vec3(-10.0f, 0.0f, 0.0f);
#else
        light->dir = glm::vec3(0.1f, -10.0f, 0.0f);
#endif
        light.sync(GL_MAP_WRITE_BIT| GL_MAP_INVALIDATE_BUFFER_BIT);

        pl.init(GL_STATIC_DRAW);
        
        pl->pls[0].pos = glm::vec3(1.f, -2.f, 0.f);
        
        pl->pls[0].color = glm::vec3(0.f,0.f, 1.f);
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

        

        fb.create();
        fb.bind();
        fb.attach_texture(*depth_tex.get(),GL_DEPTH_ATTACHMENT,0);
#ifdef PF_ANDROID
        GLenum v[] = { GL_NONE };
        glDrawBuffers(1, v);
#else
        glDrawBuffer(GL_NONE);
#endif
        glReadBuffer(GL_NONE);
        fb.unbind();
        depth_tex->unbind();

        spl.sync(GL_MAP_WRITE_BIT|GL_MAP_INVALIDATE_BUFFER_BIT);

        light_pos = glm::vec3(0.0f,0.0f,0.0f);

        for (auto& p : cxts)
            p->init();

        for (auto& p : depth_nodes)
            p->init();

        plane->init();

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
       
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f); 
        glViewport(0, 0, DEPTH_TEX_W, DEPTH_TEX_W);
        fb.bind();
        glClear(GL_DEPTH_BUFFER_BIT);
        depth_p->use();

        perspective.attach_program(depth_p);
        world.attach_program(depth_p);

        auto dep_ort = glm::ortho(-6.0f, 6.0f, 6.0f, -6.0f, 0.1f, 250.5f);
#ifdef  PF_ANDROID
        auto dep_view =  glm::lookAt(-(glm::vec3(0.1f,-10.0f,0.0f)), light_pos, glm::vec3(0.0f, 1.0f, 0.0f));
#else
        auto dep_view =  glm::lookAt(-(light->dir), light_pos, glm::vec3(0.0f, 1.0f, 0.0f));
#endif
        perspective = dep_ort;
        world = dep_view;

        perspective.sync();
        world.sync();

        for (auto& p : depth_nodes)
            p->draw();

        fb.unbind();
        

        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        blinn_p->use();
        
        perspective.attach_program(blinn_p);
        world.attach_program(blinn_p);
        
        update_matrix();

        perspective.sync();
        world.sync();

        
        
        for (auto& p : cxts)
            p->draw();

        program->use();
        
        perspective.attach_program(program);
        world.attach_program(program);
        
        update_matrix();

        perspective.sync();
        world.sync();
        lightSpaceMartix.attach_program(program);
        lightSpaceMartix = dep_ort * dep_view;
        lightSpaceMartix.sync();

        plane->draw();

        update();

    }

    void update_matrix()
    {
        perspective = glm::perspective(glm::radians(60.f), ((float)width / (float)height), 0.1f, 256.0f);
        world = glm::mat4(1.0f);

        world = glm::translate(*world, glm::vec3(0.0f, 0.0f, -4.0f));
        world = glm::rotate(*world, glm::radians(rotate.x), glm::vec3(1.f, 0.f, 0.f));
        world = glm::rotate(*world, glm::radians(rotate.y), glm::vec3(0.f, 1.f, 0.f));
        world = glm::rotate(*world, glm::radians(rotate.z), glm::vec3(0.f, 0.f, 1.f));
    }

    void update()
    {
        for (auto& p : cxts)
            p->update();

        for (auto& p : depth_nodes)
            p->update();
        
        plane->update();

        glm::vec3 pl_pos = glm::vec3(1.f, -1.f, -8.f);
        pl_pos = glm::rotateY(pl_pos,pl_angle);
        pl_pos.z = -5.6f;
        pl->pls[0].pos = pl_pos;
        pl.sync(GL_MAP_WRITE_BIT);
        if(pl_angle >= glm::pi<float>() * 2.0f) pl_angle = 0.0f; else pl_angle += 0.02f;
    }

    ~Demo1() {

    }
    void onMouseButton(int btn,int action,int mode, double x, double y) override
    {
        RenderDemoRotate::onMouseButton(btn,action,mode,x,y);
        if(btn == GLFW_MOUSE_BUTTON_2)
        {
            if(action == GLFW_PRESS)
            {
                light_pos.x += 0.1f;
            }
        }
        if(btn == GLFW_MOUSE_BUTTON_3)
        {
            if(action == GLFW_PRESS)
            {
                light_pos.x -= 0.1f;
            }
        }
        dbg(light_pos.x);
    }

    void onWindowResize(int w, int h) override
    {
        glViewport(0, 0, w, h);
    }
private:
    std::shared_ptr<Program> program,depth_p,blinn_p;
    UniformBuf<0,DictLight> light;
    Uniform<UT::Vec3> view_pos;
    GlmUniform<UT::Int> use_blinn;
    GlmUniform<UT::Matrix4> perspective;
    GlmUniform<UT::Matrix4> world;
    GlmUniform<UT::Matrix4> lightSpaceMartix;
    std::vector<std::shared_ptr< gld::Node<gld::Component>>> cxts,depth_nodes;
    UniformBuf<1,PointLights> pl;
    UniformBuf<2,SpotLight> spl;
    FrameBuffer fb;
    std::shared_ptr<Texture<TexType::D2>> depth_tex;
    glm::vec3 light_pos;
    std::shared_ptr< gld::Node<gld::Component>> plane;
    
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