#version 330 core

out vec4 color;
in vec2 oUv;
in vec4 oColor;
in vec4 oMParam0;

uniform sampler2D diffuseTex;
uniform sampler2D playerColorMask;
uniform sampler2D playerPalette;

void main()
{
    vec4 base = texture(diffuseTex, oUv);
    if (base.a < 0.01) discard;

    vec4 encoded = texture(playerColorMask, oUv);
    int debugMode = int(oMParam0.y + 0.5);
    if (debugMode == 1) {
        color = vec4(encoded.a > 0.5 ? vec3(0.0, 1.0, 0.8) : base.rgb * 0.18, base.a);
        return;
    }
    vec3 resolved = base.rgb;
    if (encoded.a > 0.5) {
        int subColor = clamp(int(encoded.r * 255.0 + 0.5), 0, 7);
        if (debugMode == 2) {
            const vec3 debugColors[8] = vec3[8](
                vec3(0.08), vec3(0.1,0.2,0.9), vec3(0.1,0.8,0.2), vec3(0.1,0.9,0.9),
                vec3(0.9,0.1,0.1), vec3(0.9,0.1,0.9), vec3(0.95,0.85,0.1), vec3(1.0));
            color = vec4(debugColors[subColor], base.a);
            return;
        }
        int player = clamp(int(oMParam0.x + 0.5), 1, 8) - 1;
        resolved = texelFetch(playerPalette, ivec2(subColor, player), 0).rgb;
        float diffuseLuma = dot(base.rgb,vec3(0.2126, 0.7152, 0.0722));
        resolved = resolved * diffuseLuma;
    }
    color = vec4(resolved, base.a) * oColor;
}
