#pragma once

#include "contents.hpp"
#include <surface.hpp>
#include <ft2pp.hpp>

namespace txt {
	typedef wws::surface<GrayContent> GraySurface;

	struct DefTextRender {
		using SurTy = GraySurface;

		inline static std::optional<std::tuple<int, int, int, unsigned int, unsigned int>> get_glyph_data(ft2::Face& face, uint32_t c)
		{
			// 8-bit anti-aliased rasterisation (grey coverage), not 1-bit MONO.
			face.load_glyph(c, FT_LOAD_DEFAULT, FT_RENDER_MODE_NORMAL);
			return face.get_glyph_data(ft2::CenterOffEx());
		}

		inline static void render(SurTy& sur, ft2::Face& face,int x,int y)
		{
			// Blit real coverage (0..255) — root-fixes the old 0x255 constant.
			face.render_surface_aa(sur, ft2::AlwaysZero(), &SurTy::set_pixel, x, y);
		}
	};
}