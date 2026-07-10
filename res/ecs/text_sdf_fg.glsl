#version 330 core

// SDF text fragment shader: distance-field fill + optional outline + drop shadow.
// The atlas stores a signed distance field in the red channel: 0.5 is the glyph
// edge, > 0.5 inside, < 0.5 outside, with a spread border baked into each glyph.
// Anti-aliasing uses the screen-space derivative (fwidth) so the fill stays
// crisp at any scale.
//
// Per-instance params (forwarded from the vertex shader):
//   oMParam0 = outline colour rgba
//   oMParam1 = (outline_width_px, shadow_softness, flags, 0)  flags: bit0 outline, bit1 shadow
//   oMParam2 = shadow colour rgba
//   oMParam3 = (shadow_off_x_px, shadow_off_y_px, 0, 0)   offset in atlas pixels
// oUvMin/oUvMax bound the glyph's own atlas rect (the quad is larger — expanded
// by a margin so an offset shadow has somewhere to draw).

#define SPREAD 8.0   // matches FreeType's SDF spread (px); maps px <-> distance

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

// Sample distance but return 0 (== "far outside") beyond the glyph rect. Same
// reasoning as the AA shader: never clamp to the rect edge (that would extrude a
// mid-distance band outward as a halo) and never let an offset sample bleed into
// a neighbouring atlas glyph.
float sdf(vec2 uv) {
    if (uv.x < oUvMin.x || uv.y < oUvMin.y || uv.x > oUvMax.x || uv.y > oUvMax.y)
        return 0.0;
    return texture(diffuseTex, uv).r;
}

void main()
{
    int flags = int(oMParam1.z + 0.5);
    bool doOutline = (flags & 1) != 0;
    bool doShadow  = (flags & 2) != 0;

    vec2 texel = 1.0 / vec2(textureSize(diffuseTex, 0));

    float d  = sdf(oUv);
    float aa = max(fwidth(d), 1e-4);              // edge softness in distance units
    float fill = smoothstep(0.5 - aa, 0.5 + aa, d);

    vec3 rgb = oColor.rgb;
    float a  = fill * oColor.a;

    if (doOutline) {
        // Outline is a band OUTSIDE the fill edge: width px -> distance offset via
        // the spread mapping (SPREAD px spans 0.5 of the normalised distance).
        // `outer` is the fill+ring silhouette; colour is the outline in the ring,
        // the text colour inside.
        float ow = oMParam1.x * 0.5 / SPREAD;
        float outer = smoothstep(0.5 - ow - aa, 0.5 - ow + aa, d);
        rgb = mix(oMParam0.rgb, oColor.rgb, fill);
        a   = outer * mix(oMParam0.a, oColor.a, fill);
    }

    if (doShadow) {
        // Distance sampled at a screen-offset position -> soft offset silhouette,
        // composited BELOW the fill/outline. softness widens the smoothstep band.
        float sd  = sdf(oUv - oMParam3.xy * texel);
        float saa = aa * max(1.0, oMParam1.y);
        float scov = smoothstep(0.5 - saa, 0.5 + saa, sd);
        float sa  = scov * oMParam2.a;
        rgb = mix(oMParam2.rgb, rgb, a);
        a   = max(a, sa);
    }

    if (a <= 0.003)
        discard;
    color = vec4(rgb, a);
}
