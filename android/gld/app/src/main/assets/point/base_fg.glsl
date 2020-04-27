#version 320 es
precision mediump float;

out vec4 color; 
in vec3 oNormal;
in vec2 oUv; 
in vec3 ofill_color;

uniform sampler2D diffuseTex;

float gray()
{
    vec2 uv = oUv + vec2(-0.5f,-0.5f);
    return 1.0f - length(uv - vec2(0.f,0.f)) * 2.0f;
}

void main() 
{ 
    vec4 t_c = texture(diffuseTex, oUv);
    if(t_c.a <= 0.1)
        discard;
    color = vec4( gray() *  ofill_color * t_c.rgb,t_c.a);
    //color = t_c;
}