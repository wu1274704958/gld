#version 330 core

// OUTLINE FRAGMENT SHADER — STUB / HOOK (architecture only).
//
// This is a placeholder that currently renders identical to the default text
// shader. Its purpose is to prove the pluggable-shader batching path: a Text
// with a TextMaterial referencing this shader is collected into its OWN batch
// (grouped by GL program id), separate from default text, and receives the
// per-instance material params below.
//
// The interface it exposes for a future real implementation:
//   oMParam0 = outline colour (rgba)
//   oMParam1 = (width_texels, softness, mode, reserved)
//
// TODO (real outline):
//   * Multi-tap the 8-bit AA coverage atlas around oUv at +/- mparam1.x texels;
//     outer = max(neighbour coverage); fill = centre coverage;
//     rgb = mix(oMParam0.rgb, oColor.rgb, fill); a = max(fill, outer)*oColor.a.
//     (Needs atlas padding >= width to avoid glyph bleed; current GAP is 2.)
//   * For crisp, scalable outlines switch the atlas to SDF (deferred) and
//     threshold the distance for fill vs outline bands.

out vec4 color;

in vec2 oUv;
in vec4 oColor;
in vec4 oMParam0;
in vec4 oMParam1;

uniform sampler2D diffuseTex;

void main()
{
    // Placeholder: plain coverage * colour (same as text_fg). Reference the
    // params so the interface/linkage is exercised without changing output.
    float a = texture(diffuseTex, oUv).r;
    if (a <= 0.01)
        discard;
    color = vec4(oColor.rgb, oColor.a * a);
    // (oMParam0 / oMParam1 intentionally unused in the stub.)
}
