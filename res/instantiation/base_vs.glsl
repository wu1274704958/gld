#version 430 core

uniform mat4 perspective;
uniform mat4 world; 
uniform mat4 model;

layout(location = 0) in vec3 vposition; 
layout(location = 1) in vec3 vnormal;
layout(location = 2) in vec2 vuv;
layout(location = 3) in mat4 vmodel;
out vec3 oNormal; 
out vec3 oVpos;
out vec2 oUv;
void main() 
{ 
    oUv = vuv;
    vec4 a = model * vec4(vposition,1.0f);
    gl_Position = perspective * world * vmodel * vec4(vposition,1.0f);
    oVpos = vec3(world * vmodel * vec4(vposition,1.0f));
    mat3 nor_mat = mat3(world * vmodel);
    oNormal = normalize(nor_mat * vnormal);
}