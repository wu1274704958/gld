#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D scene;
uniform sampler2D depthTex;
uniform vec4 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform float density;
uniform float maxAmount;
uniform float nearZ;
uniform float farZ;
uniform int hasDepth;

float linearDepth(float depth)
{
    float z = depth * 2.0 - 1.0;
    return (2.0 * nearZ * farZ) / (farZ + nearZ - z * (farZ - nearZ));
}

void main()
{
    vec4 c = texture(scene, TexCoords);
    float fog = 0.0;
    if (hasDepth == 1)
    {
        float d = linearDepth(texture(depthTex, TexCoords).r);
        float rangeFog = smoothstep(fogStart, fogEnd, d);
        float expFog = density > 0.0 ? (1.0 - exp(-density * d)) : 0.0;
        fog = max(rangeFog, expFog);
    }
    fog = clamp(fog, 0.0, maxAmount);
    FragColor = mix(c, vec4(fogColor.rgb, c.a), fog);
}
