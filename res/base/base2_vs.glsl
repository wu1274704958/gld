#version 330 core
uniform mat4 perspective;
uniform mat4 world; 
uniform mat4 model; 
layout(location = 0) in vec3 vposition; 
layout(location = 1) in vec3 vcolor;
out vec3 oColor; 
void main() 
{
    gl_Position = perspective * world * model * vec4(vposition,1.0f);
    oColor = vcolor;
}