#pragma once

#include "uniform.hpp"
#include <glm_uniform.hpp>

namespace gld{
    struct DictLight{
        DictLight()
             {}
        glm::vec3 color;
        float a;
        glm::vec3 dir;
        float b;
    };
    
    struct PointLight{
        PointLight()
            {}
        
        float constant;
        float linear;
        float quadratic;
        float a;
        glm::vec3 color;
        float b;
        glm::vec3 pos;
        float c;
    };

    struct SpotLight{
        SpotLight()
            {}
        
        float constant;
        float linear;
        float quadratic;
        float cut_off;
        glm::vec3 color;
        float a;
        glm::vec3 pos;
        float c;
        glm::vec3 dir;
        float outer_cut_off;
    };

    struct PointLights{
        PointLight pls[3];
        unsigned int len;
    };

    struct Material {
        Material(Program& p):
            diffuseTex("diffuseTex",p),
            specularTex("specularTex",p),
            ambient_strength("ambient_strength",p),
            specular_strength("specular_strength",p),
            shininess("shininess",p)
             {}
       
        GlmUniform<UT::Sampler2D> diffuseTex;
        GlmUniform<UT::Sampler2D> specularTex;
        GlmUniform<UT::Float> ambient_strength;
        GlmUniform<UT::Float> specular_strength;
        GlmUniform<UT::Float> shininess;
    };
}