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
#include "model.hpp"
#include <uniform.hpp>
#include "light.hpp"
#include <texture.hpp>
#include <comm.hpp>
#include <random>
#include <uniform_buf.hpp>

using namespace gld;
namespace fs = std::filesystem;


class Demo1 : public RenderDemoRotate {
public:
    Demo1() : view_pos("view_pos", program), perspective("perspective", program), world("world", program),
        pl(program) {}
    int init() override
    {
        RenderDemoRotate::init();
        Shader<ShaderType::VERTEX> vertex;
        Shader<ShaderType::FRAGMENT> frag;

        fs::path root = wws::find_path(3, "res", true);
        DefResMgr res_mgr(std::move(root));

        glsl::PreprocessMgr<'#',glsl::IncludePreprocess> preprocess;

        auto vs_str = res_mgr.load<ResType::text>("lighting_4/base_vs.glsl");
        auto fg_str = res_mgr.load<ResType::text>("lighting_4/base_fg.glsl");
        auto box = res_mgr.load<ResType::image>("lighting_2/container2.png",0);
        auto box_spec = res_mgr.load<ResType::image>("lighting_3/container2_specular.png",0);

        auto vs_p = vs_str.get()->c_str();
        auto fg_p = fg_str.get()->c_str();

        try {
            sundry::compile_shaders<100>(
                GL_VERTEX_SHADER, &vs_p, 1, (GLuint*)vertex,
                GL_FRAGMENT_SHADER, &fg_p, 1, (GLuint*)frag
            );
        }
        catch (sundry::CompileError e)
        {
            std::cout << "compile failed " << e.what() << std::endl;
        }
        catch (std::exception e)
        {
            std::cout << e.what() << std::endl;
        }

        std::cout << vertex.get_id() << " " << frag.get_id() << std::endl;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);

        program.cretate();
        program.attach_shader(std::move(vertex));
        program.attach_shader(std::move(frag));
        program.link();

        program.use();

        program.locat_uniforms("perspective", "world", "model", "diffuseTex", "ambient_strength",
            "specular_strength",
            "view_pos",
            "shininess","specularTex",
            "pl_constant",
            "pl_linear",
            "pl_quadratic",
            "pl_color",
            "pl_pos"
            );

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        if(box && box_spec)
        {
            diffuseTex.create();
            diffuseTex.bind();

            diffuseTex.tex_image(0,box->gl_format(),0,box->gl_format(),box->data,box->width,box->height);
            diffuseTex.generate_mipmap();

            diffuseTex.set_paramter<TexOption::WRAP_S,TexOpVal::REPEAT>();
            diffuseTex.set_paramter<TexOption::WRAP_T,TexOpVal::REPEAT>();

            diffuseTex.set_paramter<TexOption::MIN_FILTER,TexOpVal::LINEAR_MIPMAP_LINEAR>();
            diffuseTex.set_paramter<TexOption::MAG_FILTER,TexOpVal::LINEAR>();

            specularTex.create();
            specularTex.bind();

            specularTex.tex_image(0,box_spec->gl_format(),0,box_spec->gl_format(),box_spec->data,box_spec->width,box_spec->height);
            specularTex.generate_mipmap();

            specularTex.set_paramter<TexOption::WRAP_S,TexOpVal::REPEAT>();
            specularTex.set_paramter<TexOption::WRAP_T,TexOpVal::REPEAT>();

            specularTex.set_paramter<TexOption::MIN_FILTER,TexOpVal::LINEAR_MIPMAP_LINEAR>();
            specularTex.set_paramter<TexOption::MAG_FILTER,TexOpVal::LINEAR>();
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

        glm::vec3 pl_pos = glm::vec3(1.f, -2.f, 0.f);
        pl.pos = glm::value_ptr(pl_pos);

        glm::vec3 pl_c = glm::vec3(1.f, 0.f, 0.f);
        pl.color = glm::value_ptr(pl_c);
        pl.constant = 1.0f;
        pl.linear = 0.09f;
        pl.quadratic = 0.032f;


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

        program.use();

        perspective.sync();
        world.sync();

        for (auto& p : cxts)
            p->draw();

        update();
        update_matrix();

        program.unuse();
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
        pl.pos = glm::value_ptr(pl_pos);
        if(pl_angle >= glm::pi<float>() * 2.0f) pl_angle = 0.0f; else pl_angle += 0.02f;
    }

    ~Demo1() {

    }

    void onWindowResize(int w, int h) override
    {
        glViewport(0, 0, w, h);
    }
private:
    Program program;
    VertexArr va1, va2;
    UniformBuf<0,DictLight> light;
    Uniform<UT::Vec3> view_pos;
    GlmUniform<UT::Matrix4> perspective;
    GlmUniform<UT::Matrix4> world;
    std::vector<std::unique_ptr<Drawable>> cxts;
    Texture<TexType::D2> diffuseTex,specularTex;
    PointLight pl;
    float pl_angle = 0.0f;
};


int main()
{
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