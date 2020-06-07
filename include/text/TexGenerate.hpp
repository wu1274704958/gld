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
			res->tex_image(0, GL_RED, 0, GL_RED, sur.get_content().ptr, sur.w(), sur.h());

			res->set_paramter<gld::TexOption::WRAP_S, gld::TexOpVal::REPEAT>();
			res->set_paramter<gld::TexOption::WRAP_T, gld::TexOpVal::REPEAT>();

			res->set_paramter<gld::TexOption::MIN_FILTER,gld::TexOpVal::LINEAR>();
			res->set_paramter<gld::TexOption::MAG_FILTER,gld::TexOpVal::LINEAR>();

			res->generate_mipmap();

			res->unbind();
			return res;
		}

		inline static void refresh(const wws::surface<GrayContent>& sur,std::shared_ptr<GenerateTy> ptr)
		{
			if (ptr)
			{
				ptr->bind();
				ptr->tex_image(0, GL_RED, 0, GL_RED, sur.get_content().ptr, sur.w(), sur.h());
				ptr->unbind();
			}
		}
	};
}