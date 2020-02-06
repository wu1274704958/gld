#pragma once

#include "uniform.hpp"
#include <glm_uniform.hpp>

namespace gld{
    struct Light{
        Light(Program& p):
            color("light_color",p),
            dir("light_dir",p)
             {}

        Uniform<UT::Vec3> color;
        Uniform<UT::Vec3> dir;
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