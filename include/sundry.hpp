#pragma once
#include <glad/glad.h>
#include <tuple>
#include <exception>
#include <iostream>

namespace sundry
{
    
    struct CompileError : public std::exception{
        CompileError( char const *str,GLuint ty,int idx) : exception(str) ,ty(ty) , _idx(idx)
        {
           
        }
        GLuint type()
        {
            return ty;
        }
        int idx()
        {
            return _idx;
        }
        private:
            GLuint ty;
            int _idx;
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
            throw CompileError("Create shader failed!",ty,Idx);
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
            throw CompileError(buf,ty,Idx);
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


    
} // namespace sundry
