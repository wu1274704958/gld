#version 330 core

out vec4 color; 
in vec3 oColor; 

void main() 
{ 
    color = vec4( oColor,1.0f);
}