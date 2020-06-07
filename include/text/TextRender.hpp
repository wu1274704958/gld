#pragma once

#include "contents.hpp"
#include <surface.hpp>

namespace txt {
	typedef wws::surface<GrayContent> GraySurface;

	struct DefTextRender {
		using SurTy = GraySurface;

		inline static std::optional<std::tuple<int, int, int, unsigned int, unsigned int>> get_glyph_data(ft2::Face& face, uint32_t c)
		{
			face.load_glyph(c);
			return face.get_glyph_data(ft2::CenterOffEx());
		}

		inline static void render(SurTy& sur, ft2::Face& face,int x,int y)
		{
			face.render_surface(sur, ft2::AlwaysZero(), &SurTy::set_pixel, x, y, static_cast<unsigned char>(0x255));
		}
	};
}