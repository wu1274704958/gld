#version 330 core

out vec4 color; 
in vec3 oNormal;
in vec2 oUv; 

uniform vec3 fill_color;
uniform sampler2D diffuseTex;

float gray()
{
    vec2 uv = oUv + vec2(-0.5f,-0.5f);
    return 1.0f - length(uv - vec2(0.f,0.f)) * 2.0f;
}

void main() 
{ 
    color = vec4( gray() *  fill_color * texture(diffuseTex, oUv).rgb,1.0f);
    //color = vec4(  fill_color * texture(diffuseTex, oUv).rgb,1.0f);
}