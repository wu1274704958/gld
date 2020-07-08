#version 330 core

out vec4 color; 
in vec2 oUv; 
in vec4 oColor;
uniform sampler2D diffuseTex;


void main() 
{ 
    vec4 t_c = texture(diffuseTex, oUv);
    if(t_c.r <= 0.01)
        discard;
    color = vec4( oColor.rgb * t_c.r,oColor.a);
}