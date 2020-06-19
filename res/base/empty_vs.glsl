#version 330 core
uniform mat4 perspective;
uniform mat4 world; 
uniform mat4 model; 
uniform mat4 local_mat; 
layout(location = 0) in vec3 vposition;
void main() 
{
    gl_Position = perspective * world * model * local_mat * vec4(vposition,1.0f);
}