#version 330 core

out vec4 color;

in vec2 oUv;
in vec4 oColor;
in vec4 oMParam0;

uniform sampler2D diffuseTex;
uniform sampler2D playerColorTex;

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
    vec4 base = texture(diffuseTex, oUv) * oColor;
    if (base.a < 0.01)
        discard;

    float mask = texture(playerColorTex, oUv).r;
    float maskAlpha = texture(playerColorTex, oUv).a;
    if (maskAlpha > 0.5) {
        int player = int(oMParam0.x + 0.5);
        int sub = clamp(int(mask * 255.0 + 0.5), 0, 7);
        float subShade = mix(0.48, 1.18, float(sub) / 7.0);
        float diffuseLuma = dot(base.rgb, vec3(0.2126, 0.7152, 0.0722));
        float diffuseShade = clamp(diffuseLuma * 1.35 + 0.18, 0.35, 1.35);
        base.rgb = basePlayerColor(player) * subShade * diffuseShade;
    }

    color = base;
}
