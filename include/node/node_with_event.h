#pragma once

#include <event/EventHandler.h>
#include <node.hpp>
#include <component.h>
#include <text/TextMgr.hpp>

namespace gld {
	struct NodeWithEvent : public Node<Component>, public evt::EventHandler<Node<Component>> {

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
			glm::vec2 braypos; float distance; glm::vec3 pos, model_pos;
			if (coll->ray_test(e->world, e->camera_pos, e->raydir, braypos, distance, pos, model_pos))
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
	
	};
}