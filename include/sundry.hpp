#pragma once
#include <glad/glad.h>
#include <tuple>
#include <exception>
#include <iostream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <serialization.hpp>
#include <random>
#include <glm/gtc/random.hpp>

#ifdef PF_ANDROID
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES/gl.h>
#include <GLES3/gl32.h>
#endif

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
                case GL_GEOMETRY_SHADER:
                    return "GL_GEOMETRY_SHADER";
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

    template<size_t I,typename ...Args>
    void format_tup_sub(std::tuple<Args...>& tup,std::string& res,char sepa)
    {
        if constexpr( I < std::tuple_size_v<std::tuple<Args...>>)
        {
            using type = std::decay_t<std::remove_reference_t<decltype(std::get<I>(tup))>>;
            if constexpr(std::is_same_v<std::string,type> || std::is_same_v<const char*,type> || std::is_same_v<char,type>)
            {
                res += std::get<I>(tup);
            }else{
                res += wws::to_string(std::get<I>(tup));
            }
            if constexpr( I + 1 < std::tuple_size_v<std::tuple<Args...>>)
            {
                res += sepa;
                format_tup_sub<I + 1>(tup,res,sepa);
            }
        }
    }

    template<typename ...Args>
    std::string format_tup(std::tuple<Args...>& tup,char sepa,char per = '\0',char back = '\0')
    {
        std::string res;
        if(per != '\0') res += per;
        format_tup_sub<0>(tup,res,sepa);
        if(back != '\0') res += back;
        return res;
    }

    template<size_t ERR_INFO_SIZE>
    std::tuple<bool,std::unique_ptr<std::string>> check_link_state(GLuint id)
    {
        GLint compile_status = GL_TRUE;
        glGetProgramiv(id,GL_LINK_STATUS,&compile_status);
        if(!compile_status)
        {
            char buf[ERR_INFO_SIZE] = {0};
            int size = 0;
            memset(buf,0,sizeof(buf));
            glGetProgramInfoLog(id,sizeof(buf),&size,buf);
            return std::make_tuple(false,std::unique_ptr<std::string>(new std::string(buf)));
        }else{
            return std::make_tuple(true,nullptr);
        }
    }

    inline float rd_0_1()
    {
        return glm::linearRand<float>(0.f, 1.f);
    }

    inline void screencoord_to_ndc(int width, int height, int x, int y, float* nx, float* ny,bool flipY = true) 
    {
        *nx = (float)x / (float)width * 2 - 1;
        *ny = ((float)y / (float)height * 2 - 1) * (flipY ? -1.f : 1.f); // reverte y axis
    }

    inline void
        normalized2d_to_ray(float nx, float ny,glm::mat4 inverse_mvp,glm::vec3 camerapos, glm::vec3& raypos, glm::vec3& raydir) {
        // 世界坐标 - 视图矩阵 - 透视矩阵 - 透视除法 - ndc
        // 要得到反转得到世界坐标，先需要视图矩阵和透视矩阵的反转矩阵

        // ndc 坐标系是左手坐标系，所以近平面的 z 坐标为远平面的 z 坐标要小
        glm::vec4 nearpoint_ndc(nx, ny, -1, 1);
        glm::vec4 nearpoint_world = inverse_mvp * nearpoint_ndc;

        // 消除矩阵反转后，透视除法的影响
        nearpoint_world /= nearpoint_world.w;

        raypos = glm::vec3(nearpoint_world);
        raydir = raypos - camerapos;
        raydir = glm::normalize(raydir);
    }
    
} // namespace sundry
