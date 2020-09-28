#version 330 core

out vec4 color; 
in vec2 oUv; 
in vec4 oColor;
uniform sampler2D diffuseTex;
uniform int blurEdgeN;


void main() 
{ 
    float shadow = 0.f;
    vec2 texelSize = 1.0 / textureSize(diffuseTex, 0);
    for(int x = -blurEdgeN; x <= blurEdgeN; ++x)
    {
        for(int y = -blurEdgeN; y <= blurEdgeN; ++y)
        {
            float pcfDepth = texture(diffuseTex, oUv.xy + vec2(x, y) * texelSize).r; 
            shadow += pcfDepth;        
        }    
    }
    shadow /= 9.f;
    if(shadow <= 0.01f)
        discard;
    color = vec4( oColor.rgb * shadow,oColor.a);
}