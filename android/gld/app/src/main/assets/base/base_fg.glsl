#version 320 es
precision mediump float;
out vec4 color; 
in vec3 oNormal; 

uniform vec3 fill_color;

void main() 
{ 
    color = vec4( fill_color,1.0f);
}