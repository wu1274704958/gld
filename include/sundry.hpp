#pragma once
#include <glad/glad.h>
#include <tuple>
#include <exception>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>

namespace sundry
{
    
    struct CompileError : public std::runtime_error{
        inline static CompileError create( char const *str,GLuint ty,int idx)
        {
            std::string msg;
            msg += type(ty);
            msg += " index: ";
            msg += std::to_string(idx);
            msg += " msg: ";
            msg += str;
            return CompileError(msg.c_str());
        }
        inline static const char* type(GLuint ty)
        {
            switch(ty)
            {
                case GL_VERTEX_SHADER:
                    return "GL_VERTEX_SHADER";
                case GL_FRAGMENT_SHADER:
                    return "GL_FRAGMENT_SHADER";
                default:
                    return "Unimpl to do!";
            }
        }
        CompileError(const char*str) :runtime_error(str) {}
    };

    struct CompileArgs{
        GLuint ty;
        char const* const * source;
        int num;
        GLuint* out_shader;
        CompileArgs(GLuint ty, char const* const * source,int num,GLuint* out_shader):
        ty(ty),
        source(source),
        num(num),
        out_shader(out_shader)
        {}
    };

    template <int ERR_INFO_SIZE,int Idx = -1>
    void compile_shader(GLuint ty, char const* const * source,int num,GLuint* out_shader) noexcept(false)
    {
        if(!out_shader) return;
        GLuint shader = glCreateShader(ty);
        if(shader <= 0)
            throw CompileError::create("Create shader failed!",ty,Idx);
	    glShaderSource(shader,num,source, nullptr);
	    glCompileShader(shader);

        GLint compile_status = GL_TRUE;
        glGetShaderiv(shader,GL_COMPILE_STATUS,&compile_status);
        if(!compile_status)
        {
            char buf[ERR_INFO_SIZE] = {0};
            int size = 0;
            memset(buf,0,sizeof(buf));
            glGetShaderInfoLog(shader,sizeof(buf),&size,buf);
            glDeleteShader(shader);
            throw CompileError::create(buf,ty,Idx);
        }
        *out_shader = shader;
    }

    template <int ERR_INFO_SIZE,int idx,typename ...Args>
    void compile_shaders_inside(std::tuple<Args...>& args) noexcept(false)
    {
        constexpr int B = idx * 4;
        compile_shader<ERR_INFO_SIZE,idx>(std::get<B + 0>(args),std::get<B + 1>(args),std::get<B + 2>(args),std::get<B + 3>(args));
        if constexpr((std::tuple_size<std::tuple<Args...>>::value - 1) > B + 3)
        {
            compile_shaders_inside<ERR_INFO_SIZE,idx+1,Args ...>(args);
        }
    }

    template <int ERR_INFO_SIZE,typename ...Args>
    void compile_shaders(std::tuple<Args...> args) noexcept(false)
    {
        static_assert( sizeof...(Args) % 4 == 0 );
        static_assert( sizeof...(Args) > 0 );
        
        constexpr int idx = 0;
        constexpr int B = idx * 4;
        compile_shader<ERR_INFO_SIZE,idx>(std::get<B + 0>(args),std::get<B + 1>(args),std::get<B + 2>(args),std::get<B + 3>(args));
        if constexpr((std::tuple_size<std::tuple<Args...>>::value - 1) > B + 3)
        {
            compile_shaders_inside<ERR_INFO_SIZE,idx+1>(args);
        }
    }

    template <int ERR_INFO_SIZE,typename ...Args>
    void compile_shaders(Args&&... args) noexcept(false)
    {
        static_assert( sizeof...(Args) % 4 == 0 );
        static_assert( sizeof...(Args) > 0 );
        std::tuple<Args...> tup = std::make_tuple(std::forward<Args>(args)...);
        constexpr int idx = 0;
        constexpr int B = idx * 4;
        compile_shader<ERR_INFO_SIZE,idx>(std::get<B + 0>(tup),std::get<B + 1>(tup),std::get<B + 2>(tup),std::get<B + 3>(tup));
        if constexpr((std::tuple_size<std::tuple<Args...>>::value - 1) > B + 3)
        {
            compile_shaders_inside<ERR_INFO_SIZE,idx+1>(tup);
        }
    }

    template<typename GenTy = float>
    GenTy normal_dist(GenTy x,GenTy u = 0.0,GenTy o = 1.0)
    {
        return static_cast<GenTy>(1.0f / glm::sqrt(2 * glm::template pi<GenTy>() * o) * glm::exp(-glm::pow(x - u, 2.f) / (2.0f * glm::pow(o, 2.f))));
    }

    
} // namespace sundry
