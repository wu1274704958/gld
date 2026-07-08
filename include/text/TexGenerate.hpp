#pragma once

#include <texture.hpp>
#include <surface.hpp>
#include "contents.hpp"

namespace txt {
	struct TexGenerate {
		using GenerateTy = gld::Texture<gld::TexType::D2>;

		inline static std::shared_ptr<GenerateTy> generate(const wws::surface<GrayContent>& sur)
		{
			std::shared_ptr<GenerateTy> res = std::shared_ptr<GenerateTy>(new GenerateTy());
			res->create();
			res->bind();
			glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
			res->tex_image(0, GL_RED, 0, GL_RED, sur.get_content().ptr, sur.w(), sur.h());
			glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

			res->set_paramter<gld::TexOption::WRAP_S, gld::TexOpVal::REPEAT>();
			res->set_paramter<gld::TexOption::WRAP_T, gld::TexOpVal::REPEAT>();

			res->set_paramter<gld::TexOption::MIN_FILTER,gld::TexOpVal::LINEAR>();
			res->set_paramter<gld::TexOption::MAG_FILTER,gld::TexOpVal::LINEAR>();

			// No mipmap for a glyph atlas: mips are unused (MIN_FILTER=LINEAR),
			// would go stale on incremental updates, and bleed across glyphs.

			res->unbind();
			return res;
		}

		inline static void refresh(const wws::surface<GrayContent>& sur,std::shared_ptr<GenerateTy> ptr)
		{
			if (ptr)
			{
				ptr->bind();
				glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
				ptr->tex_image(0, GL_RED, 0, GL_RED, sur.get_content().ptr, sur.w(), sur.h());
				glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
				ptr->unbind();
			}
		}

		// Incremental refresh: upload only the dirty glyph rectangle via
		// glTexSubImage2D. GL_UNPACK_ROW_LENGTH lets us point straight into the
		// full atlas CPU buffer and read a w×h sub-rectangle with the atlas row
		// stride — no staging copy.
		inline static void refresh(const wws::surface<GrayContent>& sur,std::shared_ptr<GenerateTy> ptr,
			int x, int y, int w, int h)
		{
			if (ptr && w > 0 && h > 0)
			{
				ptr->bind();
				glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
				glPixelStorei(GL_UNPACK_ROW_LENGTH, sur.w());
				const unsigned char* base = sur.get_content().ptr + (static_cast<size_t>(y) * sur.w() + x);
				ptr->tex_sub_image(0, x, y, w, h, GL_RED, base);
				glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
				glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
				ptr->unbind();
			}
		}
	};
}