#version 320 es
precision mediump float;
uniform mat4 perspective;
uniform mat4 world; 
uniform mat4 model; 
layout(location = 0) in vec3 vposition;
void main() 
{
    gl_Position = perspective * world * model * vec4(vposition,1.0f);
}