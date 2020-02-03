#pragma once

#include "uniform.hpp"

namespace gld{
    struct Light{
        Light(Program& p):
            color("light_color",p),
            pos("light_pos",p)
             {}

        Uniform<UT::Vec3> color;
        Uniform<UT::Vec3> pos;
    };

    struct Material {
        Material(Program& p):
            color("obj_color",p),
            ambient_strength("ambient_strength",p),
            specular_strength("specular_strength",p),
            shininess("shininess",p)
             {}
       
        Uniform<UT::Vec3> color;
        Uniform<UT::Float> ambient_strength;
        Uniform<UT::Float> specular_strength;
        Uniform<UT::Float> shininess;
    };
}