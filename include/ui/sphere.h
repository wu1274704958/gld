#pragma once

#include "word.h"
#include <node/node_with_event.h>
#include <unordered_map>
#include <vector>
#include <type_traits>

namespace gld {
	struct Sphere : public NodeWithEvent {

		Sphere() {}
		Sphere(int equator_count, int level,float off_angle = 0.0f,float r = 0.5f) : equator_count(equator_count), level(level),
			off_angle(off_angle),m_radius(r)
		{
			
		}

		void create()
		{
			add_comp(std::shared_ptr<Transform>(new Transform()));
			standBy = gen::sphere(m_radius, equator_count, level,off_angle);
			auto [vertices,indices] = gen::cube();
			add_comp(std::shared_ptr<def::Collision>(
				new def::Collision(
					std::shared_ptr<std::vector<float>>(new std::vector<float>(std::move(vertices))),
					0, 
					std::shared_ptr<std::vector<int>>(new std::vector<int>(std::move(indices)))
				)
			));
		}

		bool add(size_t idx, std::shared_ptr<Node<Component>> n)
		{
			if (idx >= standBy.size())
				return false;
			_add(idx, n);
			return true;
		}

		std::tuple<bool,size_t> rand_add(std::shared_ptr<Node<Component>> n,bool replace = false,unsigned int s = static_cast<unsigned int>( std::time(nullptr)))
		{
			if (standBy.empty()) return { false,-1 };
			if (!replace && standBy.size() == pos_map.size())
				return { false,-1 };
			std::srand(s);
			while (true)
			{
				int idx = std::rand() % standBy.size();
				if (replace || !has(idx))
				{
					_add(idx, n);
					return { true,idx };
				}
			}
			return { false,-1 };
		}

		bool has(size_t idx)
		{
			return pos_map.find(idx) != pos_map.end();
		}

		bool remove(size_t i)
		{
			if (has(i))
			{
				std::shared_ptr<Node<Component>> p = pos_map[i];
				remove_child(p);
				pos_map.erase(i);
				return true;
			}
			return false;
		}

		void remove_all()
		{
			pos_map.clear();
			Node::remove_all();
		}

		size_t slot_count()
		{
			return standBy.size();
		}

		size_t rest_slot_count()
		{
			return standBy.size() - pos_map.size();
		}

		bool onMouseDown(evt::MouseEvent<Node<Component>>* e) override 
		{
			auto w = e->target.lock();
			if (w && e->btn == GLFW_MOUSE_BUTTON_2)
			{
				pressed_pos = e->w_pos;
				enable_mouse_move = true;
			}
			return true;
		}

		bool onMouseMove(evt::MouseEvent<Node<Component>>* e) override
		{
			auto w = e->target.lock();
			if (enable_mouse_move && w && e->btn == GLFW_MOUSE_BUTTON_2)
			{
				auto of = e->w_pos - pressed_pos;
				slot_rotate_y += of.x * slot_rotate_rate;
				slot_rotate_x += of.y * slot_rotate_rate;
				set_slot_rotate();
				//update_collision();
				pressed_pos = e->w_pos;
				return true;
			}
			return false;
		}

		bool onMouseOut(evt::MouseEvent<Node<Component>>* e) override
		{
			enable_mouse_move = false;
			return true;
		}

		bool onMouseUp(evt::MouseEvent<Node<Component>>* e) override
		{
			enable_mouse_move = false;
			return true;
		}

		void update_collision(glm::vec3 scale)
		{
			glm::mat4 matrix(1.f);
			get_comp<def::Collision>()->matrix = glm::scale(matrix, scale);
		}
		
		void set_slot_rotate()
		{
			glm::mat4 matrix(1.f);

			matrix = glm::rotate(matrix, slot_rotate_x, glm::vec3(1.f, 0.f, 0.f));
			matrix = glm::rotate(matrix, slot_rotate_y, glm::vec3(0.f, 1.f, 0.f));
			
			for (auto& v : pos_map)
			{
				if (onAddOffset)
				{
					v.second->get_comp<Transform>()->pos = (glm::mat3(matrix) * standBy[v.first]) + onAddOffset(v.second);
				}
				else {
					v.second->get_comp<Transform>()->pos = glm::mat3(matrix) * standBy[v.first];
				}
				if (onSlotChanged) onSlotChanged(v.first, v.second);
			}
		}

		float slot_rotate_x = 0.f;
		float slot_rotate_y = 0.f;
		float slot_rotate_rate = 0.007f;
		std::function<glm::vec3(const std::shared_ptr<Node<Component>>&)> onAddOffset;
		std::function<void(size_t, std::shared_ptr<Node<Component>>&)> onSlotChanged;
	protected:

		void _add(size_t idx, std::shared_ptr<Node<Component>> n)
		{
			if (has(idx))
			{
				remove_child(pos_map[idx]);
			}
			pos_map[idx] = n;
			glm::mat4 matrix(1.f);

			matrix = glm::rotate(matrix, slot_rotate_x, glm::vec3(1.f, 0.f, 0.f));
			matrix = glm::rotate(matrix, slot_rotate_y, glm::vec3(0.f, 1.f, 0.f));

			if (onAddOffset)
			{
				n->get_comp<Transform>()->pos = (glm::mat3(matrix) * standBy[idx]) + onAddOffset(n);
			}
			else {
				n->get_comp<Transform>()->pos = glm::mat3(matrix) * standBy[idx];
			}
			//printf("%f %f %f\n", standBy[idx].x, standBy[idx].y,standBy[idx].z);
			if (onSlotChanged) onSlotChanged(idx, n);
			add_child(n);
		}
		float m_radius = 0.5f;
		int equator_count = 24;
		int level = 13;
		std::unordered_map < size_t , std::shared_ptr<Node<Component>>> pos_map;
		std::vector<glm::vec3> standBy;
		float off_angle = 0.01f;
		glm::vec2 pressed_pos;
		bool enable_mouse_move = false;

	};
}