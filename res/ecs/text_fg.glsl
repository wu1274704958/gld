#version 330 core

// Single-tap sample of the 8-bit AA glyph coverage (atlas is GL_RED). The
// coverage modulates alpha (straight alpha; pair with SRC_ALPHA/1-SRC_ALPHA).
out vec4 color;

in vec2 oUv;
in vec4 oColor;

uniform sampler2D diffuseTex;

void main()
{
    float a = texture(diffuseTex, oUv).r;
    if (a <= 0.01)
        discard;
    color = vec4(oColor.rgb, oColor.a * a);
}
