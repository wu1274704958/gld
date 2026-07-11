#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D scene;      // HDR scene colour
uniform sampler2D bloomBlur;  // blurred bright-pass
uniform sampler2D depthTex;
uniform float intensity;      // bloom add strength (e.g. 1.0)
uniform float exposure;       // tone-map exposure (e.g. 1.0)
uniform float bloomFogAttenuation; // 0 = bloom ignores fog, 1 = fully fog-attenuated
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
    vec3 hdr   = texture(scene, TexCoords).rgb;
    vec3 bloom = texture(bloomBlur, TexCoords).rgb;

    float fog = 0.0;
    if (hasDepth == 1)
    {
        float d = linearDepth(texture(depthTex, TexCoords).r);
        float rangeFog = smoothstep(fogStart, fogEnd, d);
        float expFog = density > 0.0 ? (1.0 - exp(-density * d)) : 0.0;
        fog = clamp(max(rangeFog, expFog), 0.0, maxAmount);
    }
    float bloomScale = mix(1.0, max(0.0, 1.0 - fog), clamp(bloomFogAttenuation, 0.0, 1.0));

    // Additive bloom preserves the original scene appearance and only adds
    // glow where bright/emissive pixels exist. exposure is a neutral (1.0)
    // overall brightness trim. sRGB write + implicit clamp handle the rest.
    vec3 result = (hdr + bloom * intensity * bloomScale) * exposure;

    // Output is linear; GL_FRAMEBUFFER_SRGB converts to sRGB on write.
    FragColor = vec4(result, 1.0);
}
