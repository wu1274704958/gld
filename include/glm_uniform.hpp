#pragma once

#include "uniform.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace gld
{
    template<UT Ut>
    struct GlmUniform{
        using UTDataMapTy = std::remove_reference_t<typename MapUTData<static_cast<size_t>(Ut),
            UTData<UT::Float,float>,
            UTData<UT::Matrix4, glm::mat4>,
            UTData<UT::Vec3,glm::vec3>,
            UTData<UT::Vec4,glm::vec4>
        >::type>;

        GlmUniform(std::string key,Program& p) : uniform(std::move(key),p)
        {

        }
        template<typename ...Args>
        GlmUniform(std::string key,Program& p,Args&& ...args) : 
            uniform(std::move(key),p),
            data(std::forward<Args>(args)...)
        {
            
        }

        UTDataMapTy operator=(UTDataMapTy d)
        {
            data = d;
            return d;
        }

        operator UTDataMapTy& ()
        {
            return data;
        }

        UTDataMapTy& operator*()
        {
            return data;
        }

        void sync()
        {
            if constexpr (Ut == UT::Float)
            {
                uniform = data;
            }else
            if constexpr (Ut == UT::Matrix4 || Ut == UT::Vec3 || Ut == UT::Vec4)
            {
                uniform = glm::value_ptr(data);
            }
        }

        UTDataMapTy data;
        Uniform<Ut> uniform;
    };
}