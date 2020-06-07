#pragma once

#include "contents.hpp"
#include "surface.hpp"
#include <unordered_map>
#include <optional>
#include <ft2pp.hpp>

namespace txt {

	struct WordData{
		uint16_t x = 0, y = 0, w = 0, h = 0, off_x = 0, off_y = 0,advance = 0;
		WordData(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t off_x, uint16_t off_y, uint16_t advance)
			: x(x),y(y),w(w),h(h),off_x(off_x),off_y(off_y),advance(advance)
		{}
		WordData(const WordData&) = default;
		WordData () {}
	};

	template <typename Gen,typename Render>
	class Page{
	public:
		constexpr static int MAXSurfaceSize = 2048;
		using GenerateTy = typename Gen::GenerateTy;
		using SurTy = typename Render::SurTy;
		Page(std::shared_ptr<ft2::Face> f) : surface(MAXSurfaceSize,MAXSurfaceSize),face(std::move(f))
		{
			
		}

		bool test(int w,int h) const
		{
			return (curr_y + h) < surface.h();
		}

		bool test(uint32_t c) const
		{
			face->load_glyph(c);
			auto gd = face->get_glyph_data(ft2::CenterOffEx());
			if (gd)
			{
				auto [off_x, off_y, advance, w, h] = (*gd);
				return test(w, h);
			}
			else
				return false;
		}

		bool put(uint32_t c)
		{
			auto gd = Render::get_glyph_data(*face, c);
			if (gd)
			{
				auto [off_x, off_y, advance, w, h] = (*gd);
				if (!test(w, h)) return false;
				if (curr_x + w >= surface.w())
				{
					curr_x = 0;
					curr_y += curr_h > h ? curr_h : h;
					curr_h = 0;
				}
				WordData wd(static_cast<uint16_t>(curr_x), static_cast<uint16_t>(curr_y),
					static_cast<uint16_t>(w), static_cast<uint16_t>(h), static_cast<uint16_t>(off_x), static_cast<uint16_t>(off_y),
					static_cast<uint16_t>(advance));

				Render::render(surface, *face, curr_x, curr_y);

				curr_x += w;
				if (h > curr_h)
					curr_h = h;
				refresh();
			}
			else
				return false;
		}

		bool has(uint32_t c) const
		{
			return word_map.find(c) != word_map.end();
		}

		std::optional<WordData> get(uint32_t c)
		{
			if (!has(c))
			{
				return {};
			}
			return std::make_optional(word_map[c]);
		}

		const std::unordered_map<uint32_t, WordData>& get_map() const
		{
			return word_map;
		}

		std::shared_ptr<GenerateTy> get_generate()
		{
			if (!generate_ptr)
			{
				generate();
			}
			return generate_ptr;
		}

	protected:
		void generate()
		{
			if(!generate_ptr)
				generate_ptr = Gen::generate(surface);
		}

		void refresh()
		{
			if (generate_ptr)
				Gen::refresh(surface, generate_ptr);
			else
				generate();
		}

		std::unordered_map<uint32_t, WordData> word_map;
		SurTy surface;
		uint16_t curr_x = 0, curr_y = 0, curr_h = 0;
		std::shared_ptr<GenerateTy> generate_ptr;
		std::shared_ptr<ft2::Face> face;
	};
}