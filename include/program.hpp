#pragma once
#include "gl_comm.hpp"
#include <glad/glad.h>


namespace gld{
    
    template<ShaderType ST>
    class Shader{
        Shader():id(0){}
        Shader(int id):id(id){}
        Shader(const Shader<ST>& oth)=delete;
        Shader(Shader<ST> &&oth)
        {
            id = oth.id;
            oth.id = 0;
        }
        Shader<ST>& operator=(const Shader<ST>&) = delete;
        Shader<ST>& operator=(Shader<ST>&& oth)
        {
            clean();
            id = oth.id;
            oth.id = 0;
        }
        clean(){
            if(id > 0)
                glDeleteShader(id);
            id = 0;
        }
        ~Shader(){
            clean();
        }
    private:
        Glid id;
    };

    
    class Shaders{

    };

    class Program{
        void use();
        template<ShaderType ST>
        void attach_shader(Shader<ST> s)
        {

        }

    private:

        Glid program;
    }; 
}