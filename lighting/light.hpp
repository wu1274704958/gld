#pragma once

#include "uniform.hpp"
#include <glm_uniform.hpp>

namespace gld{
    struct Light{
        Light():
            color("light_color"),
            pos("light_pos")
             {}
        void attach_program(std::shared_ptr<Program> p)
        {
            color.attach_program(p);
            pos.attach_program(p);
        }

        Uniform<UT::Vec3> color;
        Uniform<UT::Vec3> pos;
    };

    struct Material {
        Material(std::shared_ptr<Program> p):
            color("obj_color",p),
            ambient_strength("ambient_strength",p),
            specular_strength("specular_strength",p),
            shininess("shininess",p)
             {}
       
        GlmUniform<UT::Vec3> color;
        GlmUniform<UT::Float> ambient_strength;
        GlmUniform<UT::Float> specular_strength;
        GlmUniform<UT::Float> shininess;
    };
}