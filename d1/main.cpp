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

#define DEL_GL(what,id,...) if(id != 0) glDelete##what(id,__VA_ARGS__)
#define DEL_GL_shader(id) DEL_GL(Shader,id)

#define DELS_GL(what,num,id) if(id != 0) glDelete##what(num,&id)
#define DELS_GL_vertex_arr(id,num) DELS_GL(VertexArrays,num,id)

BuildStr(shader_base,vs,#version 330 core\n
    uniform mat4 perspective; \n
    uniform mat4 world; \n
    uniform mat4 model; \n
    layout(location =0) in vec3 vposition; \n
    layout(location =1) in vec4 color; \n
    out vec4 outColor; \n
    void main() \n
    { \n
        gl_Position = perspective * world * model * vec4(vposition,1.0f);\n
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
};

class Demo1 : public RenderDemo{
public:
    int init() override
    {
        glfwGetWindowSize(m_window,&width,&height);
        try{
            sundry::compile_shaders<100>(
                GL_VERTEX_SHADER,&(shader_base::vs),1,&base_vs,
                GL_FRAGMENT_SHADER,&(shader_base::fs),1,&base_fs
            );
            
        }catch(sundry::CompileError e)
        {
             std::cout << "compile failed " << e.type() <<  " " << e.what()  <<std::endl;
        }catch(std::exception e)
        {
             std::cout << e.what()  <<std::endl;
        }
        
        std::cout << base_vs <<" "<< base_fs <<std::endl;
        
        glGenVertexArrays(1,&vertex_arr);
        glBindVertexArray(vertex_arr);

        glGenBuffers(1,&vertex_buff);
        glBindBuffer(GL_ARRAY_BUFFER,vertex_buff);

        Vertex vertices[] = {
            Vertex{
                glm::vec3(1.0f,1.0f,0.0f),
                glm::vec4(1.0f,0.0f,0.0f,1.0f)
            },
            Vertex{
                glm::vec3(-1.0f,0.0f,0.0f),
                glm::vec4(0.0f,1.0f,0.0f,1.0f)
            },
            Vertex{
                glm::vec3(1.0f,0.0f,0.0f),
                glm::vec4(0.0f,0.0f,1.0f,1.0f)
            }
            ,
            Vertex{
                glm::vec3(-1.0f,-1.0f,.0f),
                glm::vec4(1.0f,0.0f,1.0f,1.0f)
            }
        };

        glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
        glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,7 * sizeof(GLfloat),(void *)0);
        glEnableVertexAttribArray(0);

        glVertexAttribPointer(1,4,GL_FLOAT,GL_FALSE,7 * sizeof(GLfloat),(void *)(sizeof(GLfloat) * 3));
        glEnableVertexAttribArray(1);

        glBindBuffer(GL_ARRAY_BUFFER,0);
        glBindVertexArray(0);

        program = glCreateProgram();
        glAttachShader(program,base_vs);
        glAttachShader(program,base_fs);
        glLinkProgram(program);

        glUseProgram(program);

        perspective =   glGetUniformLocation(program,"perspective");
        world =         glGetUniformLocation(program,"world");
        model =         glGetUniformLocation(program,"model");
        alpha =         glGetUniformLocation(program,"alpha");

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
        
        glUseProgram(program);
        

        glUniform1f(alpha,1.0f);

        update_matrix();

        glUniformMatrix4fv(perspective,1,GL_FALSE,glm::value_ptr(perspective_m));
        glUniformMatrix4fv(world,1,GL_FALSE,glm::value_ptr(world_m));
        glUniformMatrix4fv(model,1,GL_FALSE,glm::value_ptr(model_m));

        glBindVertexArray(vertex_arr);
        glBindBuffer(GL_ARRAY_BUFFER,vertex_buff);

        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        //glDrawArrays(GL_TRIANGLES,0,3);
        
        glBindBuffer(GL_ARRAY_BUFFER,0);
        glBindVertexArray(0);
    }

    void update_matrix(){
        perspective_m= glm::perspective(glm::radians(60.f),((float)width/(float)height),0.1f,256.0f);
        world_m = glm::mat4(1.0f);
        model_m = glm::mat4(1.0f);

        world_m = glm::translate(world_m,glm::vec3(0.0f,0.0f,-3.0f));
    }

    ~Demo1(){
        DEL_GL_shader(base_vs);
        DEL_GL_shader(base_fs);
        DELS_GL_vertex_arr(vertex_arr,1);
    }

    std::vector<Vertex> generate_vertices(int density,float w)
    {
        float gap = 1.0f / static_cast<float>(density);
        std::vector<glm::vec3> res;
        auto get_line = [density,gap,&res](glm::vec3 b,glm::vec3 dir)
        {
            
        };
    }
private:
    GLuint base_vs = 0,base_fs = 0,
    program = 0,
    vertex_arr = 0,
    vertex_buff = 0,
    perspective = 0,
    world = 0,
    model = 0,
    alpha = 0;
    glm::mat4 perspective_m,
    world_m,
    model_m;
    int width,height;
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