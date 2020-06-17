#pragma once

#include <event/EventHandler.h>
#include <node.hpp>
#include <component.h>
#include <text/TextMgr.hpp>

namespace gld {
	struct Word : public Node<Component>, public evt::EventHandler<Node<Component>> {

		Word()
		{}

		std::vector<evt::EventHandler<Node<Component>>*> evt_childlren() override
		{
			std::vector<evt::EventHandler<Node<Component>>*> res;
			for (auto& ch : children)
			{
				try {
					auto p = dynamic_cast<evt::EventHandler<Node<Component>>*>(ch.get());
					if (p) res.push_back(p);
				}catch(std::bad_cast &e){}
			}
			return res;
		}

		bool onHandleMouseEvent(evt::MouseEvent<Node<Component>>* e) override
		{
			auto coll = this->get_comp<gld::def::Collision>();
			if (!coll) return false;
			glm::vec2 braypos; float distance; glm::vec3 pos;
			if (coll->ray_test(e->world, e->camera_pos, e->raydir, braypos, distance, pos))
			{
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