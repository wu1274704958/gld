#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D scene;
uniform float threshold;   // luminance cutoff (e.g. 0.9)
uniform float knee;        // soft-knee width (e.g. 0.4)

void main()
{
    vec3 c = texture(scene, TexCoords).rgb;
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));

    // Soft-knee threshold: smooth ramp from (threshold-knee) to (threshold+knee)
    float w = smoothstep(threshold - knee, threshold + knee, luma);

    FragColor = vec4(c * w, 1.0);
}
