#version 330 core

// Instanced glyph/sprite quad. loc0 = unit-quad position; per-instance:
// loc1 rect (glyph uv x,y,w,h), loc2 pad (shadow-expanded uv x,y,w,h),
// loc3 color, loc4..7 transform (world*local), loc8..11 material params.
uniform mat4 uViewProj;

layout(location = 0) in vec3 vposition;
layout(location = 1) in vec4 vrect;
layout(location = 2) in vec4 vpad;
layout(location = 3) in vec4 vcolor;
layout(location = 4) in mat4 vtransform;
layout(location = 8) in vec4 vmparam0;
layout(location = 9) in vec4 vmparam1;
layout(location = 10) in vec4 vmparam2;
layout(location = 11) in vec4 vmparam3;

out vec2 oUv;
out vec2 oUvMin;   // glyph rect min (sampling clamp)
out vec2 oUvMax;   // glyph rect max
out vec4 oColor;
out vec4 oMParam0;
out vec4 oMParam1;
out vec4 oMParam2;
out vec4 oMParam3;

void main()
{
    gl_Position = uViewProj * vtransform * vec4(vposition, 1.0);

    // Corner uv spans the (shadow-expanded) pad rect; ids match the shared quad
    // verts: 0 TR, 1 TL, 2 BL, 3 BR.
    vec2 p0 = vpad.xy;
    vec2 p1 = vpad.xy + vpad.zw;
    if      (gl_VertexID == 0) oUv = vec2(p1.x, p0.y);
    else if (gl_VertexID == 1) oUv = vec2(p0.x, p0.y);
    else if (gl_VertexID == 2) oUv = vec2(p0.x, p1.y);
    else                        oUv = vec2(p1.x, p1.y);

    oUvMin = vrect.xy;
    oUvMax = vrect.xy + vrect.zw;

    oColor = vcolor;
    oMParam0 = vmparam0;
    oMParam1 = vmparam1;
    oMParam2 = vmparam2;
    oMParam3 = vmparam3;
}
