#version 330 core

out vec4 color;

in vec2 oUv;
in vec4 oColor;
in vec4 oMParam0;

uniform sampler2D diffuseTex;

vec3 basePlayerColor(int player)
{
    if (player == 2) return vec3(1.00, 0.05, 0.03);
    if (player == 3) return vec3(0.02, 0.66, 0.12);
    if (player == 4) return vec3(0.84, 0.84, 0.10);
    if (player == 5) return vec3(0.48, 0.94, 0.94);
    if (player == 6) return vec3(0.56, 0.08, 0.97);
    if (player == 7) return vec3(0.40, 0.40, 0.40);
    if (player == 8) return vec3(1.00, 0.57, 0.02);
    return vec3(0.02, 0.12, 1.00);
}

void main()
{
    vec4 texel = texture(diffuseTex, oUv);
    if (texel.a < 0.01)
        discard;

    int player = int(oMParam0.x + 0.5);
    bool marker = texel.r > 0.995 && texel.b < 0.005;
    if (marker) {
        int sub = clamp(int(texel.g * 255.0 / 32.0 + 0.5), 0, 7);
        float shade = mix(0.42, 1.22, float(sub) / 7.0);
        texel.rgb = basePlayerColor(player) * shade;
    }

    color = texel * oColor;
}
