#version 450 core

#include "../lighting_4/light.glsl"
#include "comm.glsl"

layout (std140,binding = 1) uniform PL{
    PointLight pointLight[PL_LEN];
    uint pl_len;
};

layout (std140,binding = 2) uniform SPL{
    SpotLight spotLight;
};

uniform mat4 perspective;
uniform mat4 world; 
uniform mat4 model;
uniform vec3 pl_pos; 
layout(location = 0) in vec3 vposition; 
layout(location = 1) in vec3 vnormal;
layout(location = 2) in vec2 vuv;
out vec3 oNormal; 
out vec3 oVpos;
out vec2 oUv;
out PointLight o_pl[PL_LEN];
out SpotLight o_spl;
out uint o_pl_len;
void main() 
{ 
    oUv = vuv;
    gl_Position = perspective * world * model * vec4(vposition,1.0f);
    oVpos = vec3(world * model * vec4(vposition,1.0f));
    mat3 nor_mat = mat3(world * model);
    oNormal = normalize(nor_mat * vnormal);
    
    uint len = pl_len; 
    if(len > PL_LEN)
        len = PL_LEN;

    
    for(uint i = 0;i < len;++i)
    {
        o_pl[i] = pointLight[i];
        o_pl[i].pos = (world * vec4(pointLight[i].pos,0.0f)).xyz;
    }

    o_pl_len = len;

    o_spl = spotLight;
    o_spl.pos = (world * vec4(spotLight.pos,0.0f)).xyz;
}