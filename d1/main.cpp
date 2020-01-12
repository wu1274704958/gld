#include <glad/glad.h>
#include <RenderDemo.h>
#include <cstdio>
#include <macro.hpp>
#include <sundry.hpp>

BuildStr(shader_base,vs,#version 330 core\n
    uniform mat4 perspective; \n
    uniform mat4 world; \n
    uniform mat4 modle; \n
    layout(location =0) in vec3 vposition; \n
    layout(location =1) in vec4 color; \n
    out vec4 outColor; \n
    void main() \n
    { \n
        gl_Position = perspective * world * modle * vec4(vposition,1.0f);\n
        outColor = color;\n
    }
)

BuildStr(shader_base,fs,#version 330 core\n
    in vec4 in_color; \n
    out vec4 color; \n
    uniform float alpha;
    void main() \n
    { \n
        float self_alpha = alpha;
        if(self_alpha > 1.0f || self_alpha < 0.0f ) self_alpha = 1.0f;
        float a = self_alpha * in_color.a;
        color = vec4(vec3(in_color),a);\n
    }
)

class Demo1 : public RenderDemo{
public:
    int init() override
    {

        try{
            sundry::compile_shaders<100>(std::make_tuple(
                GL_VERTEX_SHADER,&(shader_base::vs),1,&base_vs,
                GL_FRAGMENT_SHADER,&(shader_base::fs),1,&base_fs
            ));
            
        }catch(sundry::CompileError e)
        {
             std::cout << "compile failed " << e.type() <<  " " << e.what()  <<std::endl;
        }catch(std::exception e)
        {
             std::cout << e.what()  <<std::endl;
        }
        
        std::cout << base_vs <<" "<< base_fs <<std::endl;
        
        return 0;
    }

    void draw() override
    {

    }
private:
    GLuint base_vs = 0,base_fs = 0;
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