#version 330 core

in  vec3 f_color;
out vec4 color;

// Set by BiMeshComp before each draw call:
//   1 → GL_POINTS : apply round soft-glow falloff via gl_PointCoord
//   0 → GL_LINES  : solid emissive colour (gl_PointCoord undefined for lines)
uniform int uIsPoint;

void main()
{
    if (uIsPoint == 1)
    {
        vec2  d = gl_PointCoord - vec2(0.5);
        float r = length(d) * 2.0;      // 0 at centre .. 1 at edge
        if (r > 1.0) discard;

        // Bright core + soft halo. HDR colour (may exceed 1.0) drives bloom.
        float glow = smoothstep(1.0, 0.0, r);
        float core = pow(glow, 1.8);
        color = vec4(f_color * core, 1.0);
    }
    else
    {
        color = vec4(f_color, 1.0);
    }
}
