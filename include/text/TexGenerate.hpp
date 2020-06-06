#pragma once

#include <texture.hpp>
#include <surface.hpp>
#include "contents.hpp"

namespace txt {
	struct TexGenerate {
		using GenerateTy = gld::Texture<gld::TexType::D2>;

		std::shared_ptr<GenerateTy> generate(const surface<GrayContent>& sur)
		{
			std::shared_ptr<GenerateTy> res = std::shared_ptr<GenerateTy>(new GenerateTy());
			res->create();
			res->bind();
			res->tex_image(0, GL_RED, 0, GL_RED, sur.get_content().ptr, sur.w(), sur.h());

			res->set_paramter<TexOption::WRAP_S, TexOpVal::REPEAT>();
			res->set_paramter<TexOption::WRAP_T, TexOpVal::REPEAT>();

			res->set_paramter<TexOption::MIN_FILTER, TexOpVal::LINEAR>();
			res->set_paramter<TexOption::MAG_FILTER, TexOpVal::LINEAR>();

			res->generate_mipmap();

			res->unbind();
			return res;
		}

		void refresh(const surface<GrayContent>& sur,std::shared_ptr<GenerateTy> ptr)
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