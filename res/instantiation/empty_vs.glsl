#version 330 core
uniform mat4 perspective;
uniform mat4 world; 
layout(location = 0) in vec3 vposition;
layout(location = 1) in mat4 vmodel;
layout(location = 5) in mat4 local_mat;
void main() 
{
    gl_Position = perspective * world * vmodel * local_mat * vec4(vposition,1.0f);
}