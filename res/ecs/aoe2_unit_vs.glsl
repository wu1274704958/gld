#version 330 core

uniform mat4 uViewProj;

layout(location = 0) in vec3 vposition;
layout(location = 1) in vec4 vaxisX;
layout(location = 2) in vec4 vaxisY;
layout(location = 3) in vec4 vorigin;
layout(location = 4) in vec4 vuv;
layout(location = 5) in vec4 vgeometry;
layout(location = 6) in uvec2 vmaterial;

out vec2 oUv;
out vec4 oColor;
out vec4 oMParam0;

vec4 unpackRgba8(uint value)
{
    return vec4(
        float( value        & 255u),
        float((value >>  8u) & 255u),
        float((value >> 16u) & 255u),
        float((value >> 24u) & 255u)) / 255.0;
}

void main()
{
    float width = vgeometry.x;
    float height = vgeometry.y;
    vec2 foot = vgeometry.zw;

    vec2 local = vec2(
        vposition.x * width + width * 0.5 - foot.x,
        vposition.y * height + foot.y - height * 0.5);
    vec4 world = vorigin + vaxisX * local.x + vaxisY * local.y;
    gl_Position = uViewProj * world;

    vec2 p0 = vuv.xy;
    vec2 p1 = vuv.xy + vuv.zw;
    if      (gl_VertexID == 0) oUv = vec2(p1.x, p0.y);
    else if (gl_VertexID == 1) oUv = vec2(p0.x, p0.y);
    else if (gl_VertexID == 2) oUv = vec2(p0.x, p1.y);
    else                        oUv = vec2(p1.x, p1.y);

    oColor = unpackRgba8(vmaterial.x);
    oMParam0 = vec4(float(vmaterial.y & 15u),
                    float((vmaterial.y >> 4u) & 3u), 0.0, 0.0);
}
