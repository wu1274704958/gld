#pragma once

// SDF glyph rasterisation backend (a txt:: Render policy, drop-in alongside
// DefTextRender). It uses FreeType's built-in FT_RENDER_MODE_SDF to produce a
// signed-distance-field bitmap per glyph (8-bit GL_RED, edge at 128, inside
// >128, outside <128, spread border baked into the bitmap). The SDF atlas is
// sampled with LINEAR filtering and thresholded in text_sdf_fg.glsl, giving
// crisp scalable fills plus cheap outline/shadow.
//
// GL upload reuses txt::TexGenerate (GL_RED + LINEAR), identical to the AA path.

#include <optional>
#include <tuple>

#include <ft2pp.hpp>
#include <text/contents.hpp>
#include <text/TextRender.hpp>   // GraySurface, DefTextRender

namespace gld::ecs {

    // ECS AA glyph render policy. Identical rasterisation to txt::DefTextRender
    // (8-bit coverage), but uses the UNCLAMPED ft2::CenterOff for the vertical
    // offset. CenterOffEx clamps off_y by glyph height, which puts short (Latin)
    // and tall (CJK) glyphs on different baselines; CenterOff keeps a common
    // baseline (off_y = ascender - bitmap_top) so mixed EN/CN text lines up.
    // (Legacy txt::DefTextRender is left untouched to not affect the old path.)
    struct EcsAaRender {
        using SurTy = txt::GraySurface;

        static std::optional<std::tuple<int, int, int, unsigned int, unsigned int>>
        get_glyph_data(ft2::Face& face, uint32_t c) {
            face.load_glyph(c, FT_LOAD_DEFAULT, FT_RENDER_MODE_NORMAL);
            return face.get_glyph_data(ft2::CenterOff());
        }

        static void render(SurTy& sur, ft2::Face& face, int x, int y) {
            face.render_surface_aa(sur, ft2::AlwaysZero(), &SurTy::set_pixel, x, y);
        }
    };

    struct SdfRender {
        using SurTy = txt::GraySurface;

        static std::optional<std::tuple<int, int, int, unsigned int, unsigned int>>
        get_glyph_data(ft2::Face& face, uint32_t c) {
            // Load the outline then render it to a signed distance field.
            // Unclamped CenterOff keeps a common baseline across glyph heights
            // (CenterOffEx's height-dependent clamp misaligns EN vs CN, and the
            // SDF spread makes rows > y_ppem so that clamp would always fire).
            face.load_glyph(c, FT_LOAD_DEFAULT, FT_RENDER_MODE_SDF);
            return face.get_glyph_data(ft2::CenterOff());
        }

        static void render(SurTy& sur, ft2::Face& face, int x, int y) {
            // Blit the SDF bytes (distance, not coverage). Cleared background is 0
            // == "far outside", consistent with the SDF's outside band.
            face.render_surface_aa(sur, ft2::AlwaysZero(), &SurTy::set_pixel, x, y);
        }
    };
}
