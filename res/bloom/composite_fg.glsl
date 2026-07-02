#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D scene;      // HDR scene colour
uniform sampler2D bloomBlur;  // blurred bright-pass
uniform float intensity;      // bloom add strength (e.g. 1.0)
uniform float exposure;       // tone-map exposure (e.g. 1.0)

void main()
{
    vec3 hdr   = texture(scene, TexCoords).rgb;
    vec3 bloom = texture(bloomBlur, TexCoords).rgb;

    // Additive bloom preserves the original scene appearance and only adds
    // glow where bright/emissive pixels exist. exposure is a neutral (1.0)
    // overall brightness trim. sRGB write + implicit clamp handle the rest.
    vec3 result = (hdr + bloom * intensity) * exposure;

    // Output is linear; GL_FRAMEBUFFER_SRGB converts to sRGB on write.
    FragColor = vec4(result, 1.0);
}
