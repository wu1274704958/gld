#version 430 core

struct DirectionalLight {
    vec4 directionIntensity; // xyz = direction to light, w = intensity
    vec4 color;
};

struct PointLight {
    vec4 positionRange;     // xyz = world position, w = range
    vec4 colorIntensity;    // rgb = color, w = intensity
};

struct SpotLight {
    vec4 positionRange;     // xyz = world position, w = range
    vec4 directionAngles;   // xyz = direction to target, w = cos(inner)
    vec4 colorIntensity;    // rgb = color, w = intensity
    vec4 params;            // x = cos(outer)
};

layout(std430, binding = 0) readonly buffer DirectionalLightBuffer {
    DirectionalLight dirLights[];
};
layout(std430, binding = 1) readonly buffer PointLightBuffer {
    PointLight pointLights[];
};
layout(std430, binding = 2) readonly buffer SpotLightBuffer {
    SpotLight spotLights[];
};

in vec2 oUv;
in vec3 oWorldPos;
in vec3 oNormal;

out vec4 FragColor;

uniform sampler2D tex;
uniform int hasTex;
uniform vec4 color;
uniform vec3 viewPos;
uniform vec3 ambientColor;
uniform float ambientStrength;
uniform int lightingEnabled;
uniform int dirLightCount;
uniform int pointLightCount;
uniform int spotLightCount;

vec3 applyLight(vec3 base, vec3 normal, vec3 lightDir, vec3 lightColor, float intensity, float attenuation)
{
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 viewDir = normalize(viewPos - oWorldPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 32.0);
    return base * lightColor * intensity * attenuation * diff
         + lightColor * intensity * attenuation * spec * 0.25;
}

void main()
{
    vec4 baseTex = (hasTex == 1) ? texture(tex, oUv) : vec4(1.0);
    vec4 base4 = baseTex * color;
    vec3 base = base4.rgb;
    vec3 normal = normalize(oNormal);

    vec3 ambient = max(ambientColor, vec3(max(ambientStrength, 0.02)));
    vec3 lit = base * ambient;

    if (lightingEnabled == 1)
    {
        for (int i = 0; i < dirLightCount; ++i)
        {
            vec3 lightDir = normalize(dirLights[i].directionIntensity.xyz);
            lit += applyLight(base, normal, lightDir, dirLights[i].color.rgb,
                              dirLights[i].directionIntensity.w, 1.0);
        }

        for (int i = 0; i < pointLightCount; ++i)
        {
            vec3 toLight = pointLights[i].positionRange.xyz - oWorldPos;
            float dist = length(toLight);
            float range = max(pointLights[i].positionRange.w, 0.001);
            float attenuation = clamp(1.0 - dist / range, 0.0, 1.0);
            attenuation *= attenuation;
            lit += applyLight(base, normal, normalize(toLight), pointLights[i].colorIntensity.rgb,
                              pointLights[i].colorIntensity.w, attenuation);
        }

        for (int i = 0; i < spotLightCount; ++i)
        {
            vec3 toLight = spotLights[i].positionRange.xyz - oWorldPos;
            float dist = length(toLight);
            float range = max(spotLights[i].positionRange.w, 0.001);
            vec3 lightDir = normalize(toLight);
            vec3 spotDir = normalize(spotLights[i].directionAngles.xyz);
            float theta = dot(-lightDir, spotDir);
            float innerCos = spotLights[i].directionAngles.w;
            float outerCos = spotLights[i].params.x;
            float cone = clamp((theta - outerCos) / max(innerCos - outerCos, 0.001), 0.0, 1.0);
            float attenuation = clamp(1.0 - dist / range, 0.0, 1.0);
            attenuation = attenuation * attenuation * cone;
            lit += applyLight(base, normal, lightDir, spotLights[i].colorIntensity.rgb,
                              spotLights[i].colorIntensity.w, attenuation);
        }
    }

    FragColor = vec4(lit, base4.a);
}
