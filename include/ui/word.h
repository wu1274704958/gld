#pragma once

#include <event/EventHandler.h>
#include <node.hpp>
#include <component.h>
#include <text/TextMgr.hpp>

namespace gld {
	struct Word : public Node<Component>, public evt::EventHandler<Node<Component>> {

		

		std::string face;
		int font_size;
		uint32_t word;
		txt::WordData wd;
		glm::vec2 anchor;
	};
}