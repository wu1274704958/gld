#pragma once

#include <memory>
#include <node.hpp>
#include <texture.hpp>

namespace txt {
	struct WordData;

	struct DefTextNodeGen {
		static std::shared_ptr<gld::Node<gld::Component>> generate(std::shared_ptr < gld::Texture<gld::TexType::D2>> tex, WordData& wd);
	};
}