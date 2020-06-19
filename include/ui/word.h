#pragma once

#include <event/EventHandler.h>
#include <node.hpp>
#include <component.h>
#include <text/TextMgr.hpp>
#include <node/node_with_event.h>

namespace gld {
	struct Word : public NodeWithEvent {

		constexpr static float WORD_SCALE = 0.001f;

		Word()
		{}

		Word(std::string& face, int size, uint32_t word, glm::vec2 anchor = glm::vec2(0.5f, 0.5f))
			: face(face) , font_size(size) ,word(word) , anchor(anchor)
		{
			
		}

		bool load()
		{
			auto [a, wd_, size] = txt::DefTexMgr::instance()->get_node(face, 0, 0, font_size, word);
			if (a)
			{
				this->wd = wd_.value();
				
				auto * ptr = dynamic_cast<Node<Component>*>(this);
				*ptr = std::move(*a);

				auto trans = ptr->get_comp<Transform>();
				trans->scale = glm::vec3(static_cast<float>(wd.w) * WORD_SCALE,static_cast<float>(wd.h) * WORD_SCALE,1.0f);
				
				return true;
			}
			return false;
		}

		std::string face;
		int font_size;
		uint32_t word;
		txt::WordData wd;
		glm::vec2 anchor;
	};
}