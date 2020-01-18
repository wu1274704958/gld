#include <glad/glad.h>
#include <RenderDemo.h>
#include <cstdio>
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

struct Vertex{
    glm::vec3 pos;
    glm::vec4 color;
    Vertex(glm::vec3 pos) : pos(pos)
    {
        color = glm::vec4(0.f,1.f,.0f,0.f);
    }
    Vertex(glm::vec3 pos,glm::vec4 color) : pos(pos),color(color){}
};

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

        va1.bind();

        vertices = generate_vertices(32,0.12f); 
        vertex_size = static_cast<int>(vertices.size());

        bg_vertices = vertices;

        va1.buffs().get<ArrayBufferType::VERTEX>().bind_data(vertices, GL_STATIC_DRAW);
        va1.buffs().get<ArrayBufferType::VERTEX>().vertex_attrib_pointer<VAP_DATA<3,float,false>,VAP_DATA<4,float,false>>();

        va1.unbind();
        //-----------------------------------------------------------

        for(auto& c : bg_vertices)
        {
            c.color = wws::make_rgba(PREPARE_STRING("#A020F0FF")).make<glm::vec4>();
            c.color.a = 0.12f;
        }

        va2.create();
        va2.create_arr<ArrayBufferType::VERTEX>();
        va2.bind();

        va2.buffs().get<ArrayBufferType::VERTEX>().bind_data(bg_vertices,GL_STATIC_DRAW);

        va2.buffs().get<ArrayBufferType::VERTEX>().vertex_attrib_pointer<
            VAP_DATA<3, float, false>, 
            VAP_DATA<4, float, false>>();

        va2.unbind();

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

        return 0;
    }

    void draw() override
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
		program.use();
        

        glUniform1f(alpha,1.0f);
        glUniform1f(offsetZ,0.0f);

        update_matrix();

        glUniformMatrix4fv(perspective,1,GL_FALSE,glm::value_ptr(perspective_m));
        glUniformMatrix4fv(world,1,GL_FALSE,glm::value_ptr(world_m));
        glUniformMatrix4fv(model,1,GL_FALSE,glm::value_ptr(model_m));

       

        va1.bind();

        update_color();
        update_vertices();
        //glDrawArrays(GL_LINE_STRIP,0,vertex_size);
        glDrawArrays(GL_TRIANGLE_STRIP,draw_b,draw_count);
        //glDrawArrays(GL_TRIANGLES,0,3);

        va2.bind();

        glUniform1f(offsetZ,0.01f);

        glDrawArrays(GL_TRIANGLE_STRIP,0,vertex_size);

        va2.unbind();

        update();
    }

    void update_matrix()
    {
        perspective_m= glm::perspective(glm::radians(60.f),((float)width/(float)height),0.1f,256.0f);
        world_m = glm::mat4(1.0f);
        model_m = glm::mat4(1.0f);

        model_m = glm::rotate(model_m,rotate_y,glm::vec3(0.f,1.f,0.f));

        world_m = glm::translate(world_m,glm::vec3(0.0f,0.0f,-3.0f));
    }

    constexpr float pi_2_1()
    {
        return glm::pi<float>() / 2.f;
    }

    void update()
    {
        ++draw_idx;
        if(draw_idx == draw_dur)
        {
            draw_idx = 0;

            draw_b += 2;

            if(draw_b + min_draw_count >= vertex_size)
            {
                draw_b = 0;
                draw_count = min_draw_count;
    
                if(rotate_y >= glm::pi<float>())
                    rotate_y = 0.f;
                else
                {
                    if(rotate_y >= pi_2_1() - 0.2f && rotate_y < pi_2_1() + 0.2f)
                        rotate_y = pi_2_1() + 0.2f;
                    else
                        rotate_y += 0.05f;
                }
            }else
            if(draw_b + draw_count >= vertex_size )
                draw_count = vertex_size - draw_b;
            else{
                if(draw_count < origin_draw_count)
                    draw_count += 2;
            }
        }
    }

    void update_color()
    {
        float of = 1.0f - normal_dist(0.f);
        int m = draw_count / 2;
        for(int i = 0;i < draw_count;++i)
        {
            float ni = static_cast<float>(i - m);
            float nd = normal_dist(ni / 2.6f);
            vertices[draw_b + i].color.a = nd + of;
            vertices[draw_b + i].color.r = nd;
        }
    }

    void update_vertices()
    {
        va1.buffs().get<ArrayBufferType::VERTEX>().bind_data(vertices, GL_STATIC_DRAW);

        va1.buffs().get<ArrayBufferType::VERTEX>().vertex_attrib_pointer<
            VAP_DATA<3, float, false>,
            VAP_DATA<4, float, false>>();
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

    float normal_dist(float x)
    {
        float u = 0.0f,o = 1.0f;
        return static_cast<float>(1.0f / glm::sqrt(2 * glm::pi<float>() * o) * glm::exp( -glm::pow(x - u,2.f) / (2.0f * glm::pow(o,2.f))));
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
    world_m,
    model_m;
    int vertex_size;
    int draw_b = 0,draw_count = 4,origin_draw_count = 16,min_draw_count = 4;
    int draw_dur = 1;
    int draw_idx = 0;
    std::vector<Vertex> vertices,bg_vertices;
    float rotate_y = 0.f;
    VertexArr va1,va2;
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