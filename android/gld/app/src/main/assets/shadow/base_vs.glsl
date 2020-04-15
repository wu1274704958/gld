#version 320 es
precision mediump float;

uniform mat4 perspective;
uniform mat4 world; 
uniform mat4 model;
uniform vec3 pl_pos; 
uniform mat4 lightSpaceMartix;
layout(location = 0) in vec3 vposition; 
layout(location = 1) in vec3 vnormal;
layout(location = 2) in vec2 vuv;
out vec3 oNormal; 
out vec3 oVpos;
out vec2 oUv;
out vec4 fragPosLightSpace;
void main() 
{ 
    oUv = vuv;
    gl_Position = perspective * world * model * vec4(vposition,1.0f);
    oVpos = vec3(world * model * vec4(vposition,1.0f));
    mat3 nor_mat = mat3(world * model);
    oNormal = normalize(nor_mat * vnormal);
    fragPosLightSpace = lightSpaceMartix * model * vec4(vposition,1.0f);
}