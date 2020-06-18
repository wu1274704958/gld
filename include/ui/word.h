#pragma once

#include <event/EventHandler.h>
#include <node.hpp>
#include <component.h>
#include <text/TextMgr.hpp>

namespace gld {
	struct Word : public Node<Component>, public evt::EventHandler<Node<Component>> {

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

		int evt_children_count() override
		{
			return static_cast<int>(children.size());
		}

		evt::EventHandler<Node<Component>>* evt_child(int i) override
		{
			try {
				return dynamic_cast<evt::EventHandler<Node<Component>>*>(children[i].get());
			}
			catch (std::bad_cast& e) { return nullptr; }
			return nullptr;
		}

		bool onHandleMouseEvent(evt::MouseEvent<Node<Component>>* e) override
		{
			auto coll = this->get_comp<gld::def::Collision>();
			if (!coll) return false;
			glm::vec2 braypos; float distance; glm::vec3 pos,model_pos;
			if (coll->ray_test(e->world, e->camera_pos, e->raydir, braypos, distance, pos,model_pos))
			{
				e->model_pos = model_pos;
				e->pos = pos;
				return true;
			}
			return false;
		}

		std::weak_ptr<Node<Component>> get_target() override
		{
			auto ptr = dynamic_cast<Node<Component>*>(this);
			return ptr->weak_from_this();
		}

		std::string face;
		int font_size;
		uint32_t word;
		txt::WordData wd;
		glm::vec2 anchor;
	};
}