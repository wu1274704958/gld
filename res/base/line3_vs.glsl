#version 330 core
uniform mat4 perspective;
uniform mat4 world; 
uniform mat4 model; 
layout(location = 0) in vec3 vposition;
layout(location = 1) in vec3 vcolor;

out VS_OUT {
    vec3 f_color;
} vs_out;

void main() 
{
    gl_Position = perspective * world * model * vec4(vposition,1.0f);
    vs_out.f_color = vcolor;
}