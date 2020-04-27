#version 320 es
precision mediump float;
uniform mat4 perspective;
uniform mat4 world; 
uniform mat4 model; 
layout(location = 0) in vec3 vposition; 
layout(location = 1) in vec3 vnormal;
layout(location = 2) in vec2 vuv;
layout(location = 3) in mat4 vmodel;
layout(location = 7) in vec3 fill_color;
out vec3 oNormal; 
out vec2 oUv;
out vec3 ofill_color;
void main() 
{
    oUv = vuv;
    gl_Position = perspective * world * model * vmodel * vec4(vposition,1.0f);
    mat3 nor_mat = mat3(world * model * vmodel);
    oNormal = normalize(nor_mat * vnormal);
    ofill_color = fill_color;
}