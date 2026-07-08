#version 330 core

out vec4 color; 
in vec2 oUv; 
in vec4 oColor;
uniform sampler2D diffuseTex;
uniform int blurEdgeN;


void main() 
{ 
    float a;
    if(blurEdgeN <= 0)
    {
        // Crisp single tap on the anti-aliased glyph coverage.
        a = texture(diffuseTex, oUv).r;
    }
    else
    {
        // Optional soft edge: box average with the CORRECT divisor.
        float sum = 0.f;
        vec2 texelSize = 1.0 / textureSize(diffuseTex, 0);
        for(int x = -blurEdgeN; x <= blurEdgeN; ++x)
        {
            for(int y = -blurEdgeN; y <= blurEdgeN; ++y)
            {
                sum += texture(diffuseTex, oUv.xy + vec2(x, y) * texelSize).r;
            }
        }
        float n = float((2 * blurEdgeN + 1) * (2 * blurEdgeN + 1));
        a = sum / n;
    }
    if(a <= 0.01f)
        discard;
    color = vec4( oColor.rgb * a, oColor.a);
}
