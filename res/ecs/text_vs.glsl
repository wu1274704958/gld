#version 330 core

// Instanced glyph/sprite quad. loc0 = unit-quad position; per-instance:
// loc1 uv, loc2 uv2, loc3 color, loc4..7 model (entity world), loc8..11 local.
uniform mat4 uViewProj;

layout(location = 0) in vec3 vposition;
layout(location = 1) in vec4 vuv;
layout(location = 2) in vec4 vuv2;
layout(location = 3) in vec4 vcolor;
layout(location = 4) in mat4 vmodel;
layout(location = 8) in mat4 vlocal;
layout(location = 12) in vec4 vmparam0;
layout(location = 13) in vec4 vmparam1;

out vec2 oUv;
out vec4 oColor;
out vec4 oMParam0;
out vec4 oMParam1;

void main()
{
    gl_Position = uViewProj * vmodel * vlocal * vec4(vposition, 1.0);

    if      (gl_VertexID == 0) oUv = vuv.xy;   // TR
    else if (gl_VertexID == 1) oUv = vuv.zw;   // TL
    else if (gl_VertexID == 2) oUv = vuv2.xy;  // BL
    else                        oUv = vuv2.zw; // BR

    oColor = vcolor;
    oMParam0 = vmparam0;
    oMParam1 = vmparam1;
}
