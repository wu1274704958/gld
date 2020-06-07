#version 330 core

out vec4 color; 
in vec2 oUv; 

uniform sampler2D diffuseTex;
uniform vec4 textColor;


void main() 
{ 
    vec4 t_c = texture(diffuseTex, oUv);
    if(t_c.r <= 0.01)
        discard;
    color = vec4( textColor.rgb * t_c.r,textColor.a);
}