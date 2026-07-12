#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec3 aNormal;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

out vec2 oUv;
out vec3 oWorldPos;
out vec3 oNormal;

void main()
{
    vec4 worldPos = model * vec4(aPos, 1.0);
    gl_Position = projection * view * worldPos;
    oWorldPos = worldPos.xyz;
    oNormal = mat3(transpose(inverse(model))) * aNormal;
    oUv = aUv;
}
