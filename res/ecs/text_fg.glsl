#version 330 core

// AA text fragment shader: 8-bit coverage fill + optional drop shadow.
// The atlas stores anti-aliased coverage (0..1) in the red channel; coverage is
// used directly as alpha. Outline needs the distance field and is NOT supported
// here (see text_sdf_fg.glsl); the outline flag is ignored.
//
// Per-instance params (forwarded from the vertex shader):
//   oMParam1 = (outline_width_px, shadow_softness, flags, 0)  flags: bit1 = shadow
//   oMParam2 = shadow colour rgba
//   oMParam3 = (shadow_off_x_px, shadow_off_y_px, 0, 0)   offset in atlas pixels
// oUvMin/oUvMax bound the glyph's own atlas rect (the quad is larger — it was
// expanded by a margin so an offset shadow has somewhere to draw).

out vec4 color;

in vec2 oUv;
in vec2 oUvMin;
in vec2 oUvMax;
in vec4 oColor;
in vec4 oMParam0;
in vec4 oMParam1;
in vec4 oMParam2;
in vec4 oMParam3;

uniform sampler2D diffuseTex;

// Sample coverage but return 0 outside the glyph rect. Returning 0 (instead of
// clamping the uv to the rect edge) is essential: clamping would project every
// margin fragment onto the nearest edge texel, extruding the glyph's edge ink
// outward on all sides -> a halo around the whole glyph instead of an offset
// shadow. Returning 0 also stops an offset sample from bleeding into a
// neighbouring glyph packed nearby in the atlas.
float sampleR(vec2 uv) {
    if (uv.x < oUvMin.x || uv.y < oUvMin.y || uv.x > oUvMax.x || uv.y > oUvMax.y)
        return 0.0;
    return texture(diffuseTex, uv).r;
}

void main()
{
    int flags = int(oMParam1.z + 0.5);
    bool doShadow = (flags & 2) != 0;

    vec2 texel = 1.0 / vec2(textureSize(diffuseTex, 0));

    float fill = sampleR(oUv);        // glyph coverage at this fragment
    vec3 rgb = oColor.rgb;
    float a = fill * oColor.a;

    if (doShadow) {
        // Shadow = the glyph coverage sampled at a screen-offset position. On the
        // offset side, oUv - offset maps back into the rect -> real ink -> shadow;
        // elsewhere sampleR returns 0. Composite the fill OVER the shadow.
        float sc = sampleR(oUv - oMParam3.xy * texel);
        float sa = sc * oMParam2.a;
        rgb = mix(oMParam2.rgb, rgb, a);   // shadow colour where there's no fill
        a   = max(a, sa);
    }

    if (a <= 0.003)
        discard;
    color = vec4(rgb, a);
}
