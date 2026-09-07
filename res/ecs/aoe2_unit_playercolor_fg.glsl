#version 330 core

out vec4 color;
in vec2 oUv;
in vec4 oColor;
in vec4 oMParam0;

uniform sampler2D diffuseTex;
uniform sampler2D playerColorMask;

const vec3 teamColors[8] = vec3[8](
    vec3(0.000, 0.000, 1.000),
    vec3(1.000, 0.000, 0.000),
    vec3(0.000, 0.663, 0.106),
    vec3(0.839, 0.839, 0.106),
    vec3(0.482, 0.937, 0.941),
    vec3(0.541, 0.075, 0.969),
    vec3(0.400, 0.400, 0.400),
    vec3(1.000, 0.573, 0.020)
);

void main()
{
    vec4 base = texture(diffuseTex, oUv);
    if (base.a < 0.01) discard;

    float teamWeight = texture(playerColorMask, oUv).r;
    int debugMode = int(oMParam0.y + 0.5);
    if (debugMode != 0) {
        color = vec4(vec3(teamWeight), 1.0);
        return;
    }
    vec3 resolved = base.rgb;
    if (teamWeight > 0.0) {
        int player = clamp(int(oMParam0.x + 0.5), 1, 8) - 1;
        vec3 teamBase = teamColors[player];
        float diffuseLuma = dot(base.rgb, vec3(0.299, 0.587, 0.114));
        float shade = clamp(diffuseLuma * 1.6, 0.22, 1.15);
        vec3 teamColored = clamp(teamBase * shade, 0.0, 1.0);
        resolved = mix(base.rgb, teamColored, teamWeight);
    }
    color = vec4(resolved, base.a) * oColor;
}
