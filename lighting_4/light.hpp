#pragma once

#include "uniform.hpp"
#include <glm_uniform.hpp>

namespace gld{
    struct DictLight{
        DictLight()
             {}

        glm::vec3 color;
        glm::vec3 dir;
    };

    struct PointLight{
        PointLight(Program& p):
            color("pl_color",p),
            pos("pl_pos",p),
            constant("pl_constant",p),
            linear("pl_linear",p),
            quadratic("pl_quadratic",p)
            {}
        Uniform<UT::Vec3> color;
        Uniform<UT::Vec3> pos;
        Uniform<UT::Float> constant;
        Uniform<UT::Float> linear;
        Uniform<UT::Float> quadratic;
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