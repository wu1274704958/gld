#include <glad/glad.h>
#include <RenderDemoRotate.hpp>
#include <cstdio>
#include <memory>
#include <macro.hpp>
#include <sundry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
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

using namespace gld;

BuildStr(shader_base,vs,#version 330 core\n
    uniform mat4 perspective; \n
    uniform mat4 world; \n
    uniform mat4 model; \n
    layout(location =0) in vec3 vposition; \n
    layout(location =1) in vec3 vnormal; \n
    out vec3 oNormal; \n
    out vec3 oVpos; \n
    void main() \n
    { \n
        gl_Position = perspective * world * model * vec4(vposition,1.0f);\n
        oVpos = vec3(world * model * vec4(vposition,1.0f));\n
        mat3 nor_mat = mat3(world * model);\n
        oNormal = normalize(nor_mat * vnormal);\n
    }
)

BuildStr(shader_base,fs,#version 330 core\n
    in vec3 oNormal; \n
    in vec3 oVpos; \n
    out vec4 color; \n
    uniform vec3 obj_color;\n
    uniform vec3 light_color;\n
    uniform float ambient_strength;\n
    uniform float specular_strength;\n
    uniform float shininess;\n

    uniform vec3 light_pos; \n
    uniform vec3 view_pos;\n
    void main() \n
    { \n
        vec3 light_dir = normalize(light_pos - oVpos);\n
    
        vec3 view_dir = normalize(view_pos - oVpos);\n

        vec3 ambient = light_color * ambient_strength;\n

        vec3 diffuse = obj_color * max(dot(light_dir,oNormal),0.0f);

        vec3 light_reflect = reflect(-light_dir,oNormal);

        float spec = pow(max(dot(view_dir,light_reflect),0.0f),shininess);

        vec3 specular = specular_strength * spec * light_color;

        color = vec4( (ambient + diffuse + specular),1.0f);\n
    }
)



class Demo1 : public RenderDemoRotate{
public:
    int init() override
    {
        RenderDemoRotate::init();
        Shader<ShaderType::VERTEX> vertex;
        Shader<ShaderType::FRAGMENT> frag;
        try{
            sundry::compile_shaders<100>(
                GL_VERTEX_SHADER,&(shader_base::vs),1,(GLuint*)vertex,
                GL_FRAGMENT_SHADER,&(shader_base::fs),1,(GLuint*)frag
            );
            
        }catch(sundry::CompileError e)
        {
             std::cout << "compile failed " << e.what()  <<std::endl;
        }catch(std::exception e)
        {
             std::cout << e.what()  <<std::endl;
        }
        
        std::cout << vertex.get_id() <<" "<< frag.get_id() <<std::endl;
        
        glEnable (GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

        glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);

		program.cretate();
		program.attach_shader(std::move(vertex));
		program.attach_shader(std::move(frag));
		program.link();

		program.use();

        program.locat_uniforms("perspective","world","model","light_pos","obj_color","light_color","ambient_strength",
            "specular_strength",
            "view_pos",
            "shininess");

        perspective =   program.uniform_id("perspective");
        world =         program.uniform_id("world");
        model =         program.uniform_id("model");
        light_pos =     program.uniform_id("light_pos");
        obj_color =     program.uniform_id("obj_color");
        light_color =   program.uniform_id("light_color");
        ambient_strength=program.uniform_id("ambient_strength");
        specular_strength = program.uniform_id("specular_strength");
        view_pos = program.uniform_id("view_pos");

        glClearColor(0.0f,0.0f,0.0f,1.0f);

        dbg(perspective);
        dbg(world);
        dbg(model);
        dbg(light_pos);
        dbg(obj_color);
        dbg(std::make_tuple(light_color,ambient_strength,specular_strength,view_pos,program.uniform_id("shininess")));
        GLenum err = glGetError();
        dbg(err);

        glUniform1f(ambient_strength,0.1f);
        glm::vec3 light_c = glm::vec3(1.f,1.f,1.f);
        glUniform3fv(light_color,1,glm::value_ptr(light_c));
        glm::vec3 light_p = glm::vec3(-1.f,1.f,-1.f);
        glUniform3fv(light_pos,1,glm::value_ptr(light_p));
        glm::vec3 view_p = glm::vec3(0.0f,0.0f,0.0f);
        glUniform3fv(view_pos,1,glm::value_ptr(view_p));
        glUniform1f(specular_strength,0.7f);
        glUniform1f(program.uniform_id("shininess"),32.0f);

        float vertices[] = {
            -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
             0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
             0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
             0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
            -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
            -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
    
            -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
             0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
            -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
            -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
    
            -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
            -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
    
             0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
             0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
             0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
             0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
             0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
             0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
    
            -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
             0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
             0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
             0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
            -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
            -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
    
            -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
             0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
             0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
            -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
            -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f
        };

        va1.create();
        va1.create_arr<ArrayBufferType::VERTEX>();

        va1.bind();
        va1.buffs().get<ArrayBufferType::VERTEX>().bind_data(vertices,GL_STATIC_DRAW);
        va1.buffs().get<ArrayBufferType::VERTEX>().vertex_attrib_pointer<VAP_DATA<3,float,false>,VAP_DATA<3,float,false>>();
        va1.unbind();

        cxts.push_back(std::unique_ptr<Model>(new Model(program,va1,wws::make_rgb(PREPARE_STRING("#191970")).make<glm::vec3>(),12)));

        cxts[0]->scale = glm::vec3(1.f,1.f,1.f);

        update_matrix();

        for(auto& p : cxts)
            p->init();

        return 0;
    }

    void draw() override
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
		program.use();

        glUniformMatrix4fv(perspective, 1, GL_FALSE, glm::value_ptr(perspective_m));
        glUniformMatrix4fv(world, 1, GL_FALSE, glm::value_ptr(world_m));
        
        for (auto& p : cxts)
            p->draw();

        update();
        update_matrix();

		program.unuse();
    }

    void update_matrix()
    {
        perspective_m= glm::perspective(glm::radians(60.f),((float)width/(float)height),0.1f,256.0f);
        world_m = glm::mat4(1.0f);

        world_m = glm::translate(world_m,glm::vec3(0.0f,0.0f,-3.0f));

        world_m = glm::rotate(world_m, glm::radians(rotate.x), glm::vec3(1.f, 0.f, 0.f));
        world_m = glm::rotate(world_m, glm::radians(rotate.y), glm::vec3(0.f, 1.f, 0.f));
        world_m = glm::rotate(world_m, glm::radians(rotate.z), glm::vec3(0.f, 0.f, 1.f));
    }

    void update()
    {
        for (auto& p : cxts)
            p->update();
    }

    ~Demo1(){
        
    }

    void onWindowResize(int w, int h) override
    {
        glViewport(0, 0, w, h);
    }
private:
    Program program;
    int perspective = -1,
    world = -1,
    model = -1,
    light_pos = -1,
    obj_color = -1,
    light_color = -1,
    ambient_strength = -1,
    specular_strength = -1,
    view_pos = -1;
    glm::mat4 perspective_m,
    world_m;
    VertexArr va1,va2;
    //std::unique_ptr<View1> bg;
    std::vector<std::unique_ptr<Drawable>> cxts;
};


int main()
{
    Demo1 d;
    if( d.initWindow(800,800,"Demo1"))
    {
        printf("init window failed\n");
        return -1;
    }
    d.init();
    d.run();

    return 0;
}