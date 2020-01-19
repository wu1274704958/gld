#include <glad/glad.h>
#include <RenderDemo.h>
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
#include "view1.hpp"
#include "view2.hpp"

using namespace gld;

#define DEL_GL(what,id,...) if(id != 0) glDelete##what(id,__VA_ARGS__)
#define DEL_GL_shader(id) DEL_GL(Shader,id)

#define DELS_GL(what,num,id) if(id != 0) glDelete##what(num,&id)
#define DELS_GL_vertex_arr(id,num) DELS_GL(VertexArrays,num,id)
#define DELS_GL_buffer(id,num) DELS_GL(Buffers,num,id)

BuildStr(shader_base,vs,#version 330 core\n
    uniform mat4 perspective; \n
    uniform mat4 world; \n
    uniform mat4 model; \n
    uniform float offsetZ;
    layout(location =0) in vec3 vposition; \n
    layout(location =1) in vec4 color; \n
    out vec4 outColor; \n
    void main() \n
    { \n
        gl_Position = perspective * world * model * vec4(vposition.x,vposition.y,vposition.z + offsetZ,1.0f);\n
        outColor = color;\n
    }
)

BuildStr(shader_base,fs,#version 330 core\n
    in vec4 outColor; \n
    out vec4 color; \n
    uniform float alpha;
    void main() \n
    { \n
        float self_alpha = alpha;
        if(self_alpha > 1.0f || self_alpha < 0.0f ) self_alpha = 1.0f;
        float a = self_alpha * outColor.a;
        color = vec4(vec3(outColor),a);\n
    }
)



class Demo1 : public RenderDemo{
public:
    int init() override
    {
        RenderDemo::init();
        Shader<ShaderType::VERTEX> vertex;
        Shader<ShaderType::FRAGMENT> frag;
        try{
            sundry::compile_shaders<100>(
                GL_VERTEX_SHADER,&(shader_base::vs),1,(GLuint*)vertex,
                GL_FRAGMENT_SHADER,&(shader_base::fs),1,(GLuint*)frag
            );
            
        }catch(sundry::CompileError e)
        {
             std::cout << "compile failed " << e.type() <<  " " << e.what()  <<std::endl;
        }catch(std::exception e)
        {
             std::cout << e.what()  <<std::endl;
        }
        
        std::cout << vertex.get_id() <<" "<< frag.get_id() <<std::endl;
        
        glEnable (GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

        glDisable(GL_CULL_FACE);

        
        va1.create();
        va1.create_arr<ArrayBufferType::VERTEX>();

        vertices = generate_vertices(32,0.12f); 

        va2.create();
        va2.create_arr<ArrayBufferType::VERTEX>();

		program.cretate();
		program.attach_shader(std::move(vertex));
		program.attach_shader(std::move(frag));
		program.link();

		program.use();

        program.locat_uniforms("perspective","world","model","alpha","offsetZ");

        perspective =   program.uniform_id("perspective");
        world =         program.uniform_id("world");
        model =         program.uniform_id("model");
        alpha =         program.uniform_id("alpha");
        offsetZ =       program.uniform_id("offsetZ");

        glClearColor(0.0f,0.0f,0.0f,1.0f);

        dbg(perspective);
        dbg(world);
        dbg(model);
        dbg(alpha);
        GLenum err = glGetError();
        dbg(err);

        bg = std::unique_ptr<View1>(new View1(program, va2, vertices, glm::vec4(0.f, 1.0f, 0.f, 0.4f)));
        bg->init();

        cxt = std::unique_ptr<View2>(new View2(program, va1, std::move(vertices), glm::vec4(0.f, 1.0f, 0.f, 1.f)));
        cxt->init();

        return 0;
    }

    void draw() override
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
		program.use();

        glUniformMatrix4fv(perspective, 1, GL_FALSE, glm::value_ptr(perspective_m));
        glUniformMatrix4fv(world, 1, GL_FALSE, glm::value_ptr(world_m));

        bg->draw();
        cxt->draw();

        update();
        update_matrix();
    }

    void update_matrix()
    {
        perspective_m= glm::perspective(glm::radians(60.f),((float)width/(float)height),0.1f,256.0f);
        world_m = glm::mat4(1.0f);

        world_m = glm::translate(world_m,glm::vec3(0.0f,0.0f,-3.0f));
    }

    void update()
    {
        bg->update();
        cxt->update();
    }

    ~Demo1(){
        
    }

    std::vector<Vertex> generate_vertices(int density,float w)
    {
        float gap = 1.0f / static_cast<float>(density);
        std::vector<glm::vec3> res;
        auto get_line = [density,gap](std::vector<glm::vec3>& res,glm::vec3 b,glm::vec3 dir)
        {
            for(int i = 0;i < density;++i)
            {
                res.push_back(b + (dir * (gap * i)));
            }
        };

        get_line(res,glm::vec3(0.f,0.f,0.f),glm::vec3(0.f,-1.0f,0.f));
        get_line(res,glm::vec3(0.f,-1.f,0.f),glm::vec3(1.f,0.0f,0.f));
        get_line(res,glm::vec3(1.f,-1.f,0.f),glm::vec3(0.f,1.0f,0.f));
        get_line(res,glm::vec3(1.f,0.f,0.f),glm::vec3(-1.f,0.0f,0.f));

        res.push_back(glm::vec3(0.f,0.f,0.f));

        std::vector<glm::vec3> res2;
        glm::mat2 m(1.0f);
        float r = glm::radians(45.f);
        m[0][0] = glm::cos(r);
        m[0][1] = -glm::sin(r);
        m[1][0] = glm::sin(r);
        m[1][1] = glm::cos(r);
        for(auto &v : res)
        {
            v.x -= 0.5f;
            v.y += 0.5f;

            glm::vec2 t = m * glm::vec2(v.x,v.y);
            v.x = t.x;
            v.y = t.y;
            res2.push_back(v * (1.0f - w));
        }
        std::vector<Vertex> res3;
        for(int i = 0;i < res.size() * 2;++i)
        {
            if(i % 2 == 0)
                res3.push_back(res[i / 2]);
            else
                res3.push_back(res2[i / 2]);
        }

        return res3;
    }

    

    void onWindowResize(int w, int h) override
    {
        glViewport(0, 0, w, h);
    }
private:
    Program program;
    GLuint perspective = 0,
    world = 0,
    model = 0,
    alpha = 0,
    offsetZ = 0;
    glm::mat4 perspective_m,
    world_m;
    
    std::vector<Vertex> vertices;
    VertexArr va1,va2;
    std::unique_ptr<View1> bg;
    std::unique_ptr<View2> cxt;
};


int main()
{
    Demo1 d;
    if( d.initWindow(300,300,"Demo1"))
    {
        printf("init window failed\n");
        return -1;
    }
    d.init();
    d.run();

    return 0;
}