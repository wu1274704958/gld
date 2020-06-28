#pragma once

#include "word.h"
#include <node/node_with_event.h>
#include <unordered_map>
#include <vector>
#include <type_traits>

namespace gld {
	struct Sphere : public NodeWithEvent {

		Sphere() {}
		Sphere(int equator_count, int level) : equator_count(equator_count), level(level)
		{
			
		}

		void create()
		{
			add_comp(std::shared_ptr<Transform>(new Transform()));
			float m = glm::pi<float>() / 2.f;
			std::vector <std::tuple<float, float,int>> v;
			float angle = 0.f;
			for(int i = 0;i < level / 2;++i)
			{ 
				float a =  0.5f - ((float)i / (float)level);
				float b = glm::sqrt( glm::pow(0.5f, 2.0f) - glm::pow(a, 2.f) );
				int seg = (int)(b * (float)equator_count);

				v.push_back(std::make_tuple(a,b, seg));

				auto res = gen::circle(a, b, seg);
				

				for (auto& r : res)
					standBy.push_back(glm::rotateY(r,angle));
				angle += off_angle;
			}
			
			if (level % 2 != 0)
			{
				auto res = gen::circle(0.f, 0.5f, equator_count);

				for (auto& r : res)
					standBy.push_back(glm::rotateY(r, angle));
			}
			angle += off_angle;
			for (int i = 0; i < v.size(); ++i)
			{
				auto [a, b, seg] = v[i];
				auto res = gen::circle(-a, b, seg);

				for (auto& r : res)
					standBy.push_back(glm::rotateY(r, angle));
				angle -= off_angle;
			}
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
				pressed_pos = e->pos;
			return true;
		}

		bool onMouseMove(evt::MouseEvent<Node<Component>>* e) override
		{
			auto w = e->target.lock();
			if (w && e->btn == GLFW_MOUSE_BUTTON_2)
			{
				auto of = e->pos - pressed_pos;
				slot_rotate_y += of.x * slot_rotate_rate;
				slot_rotate_x -= of.y * slot_rotate_rate;
				set_slot_rotate();
				pressed_pos = e->pos;
				return true;
			}
			return false;
		}

		
		void set_slot_rotate()
		{
			glm::mat4 matrix(1.f);

			matrix = glm::rotate(matrix, slot_rotate_x, glm::vec3(1.f, 0.f, 0.f));
			matrix = glm::rotate(matrix, slot_rotate_y, glm::vec3(0.f, 1.f, 0.f));

			for (auto& v : pos_map)
			{
				v.second->get_comp<Transform>()->pos = glm::mat3(matrix) * standBy[v.first];
			}
		}

		float slot_rotate_x = 0.f;
		float slot_rotate_y = 0.f;
		float slot_rotate_rate = 0.7f;
		std::function<glm::vec3(const std::shared_ptr<Node<Component>>&)> onAddOffset;
	protected:

		void _add(size_t idx, std::shared_ptr<Node<Component>> n)
		{
			if (has(idx))
			{
				remove_child(pos_map[idx]);
			}
			pos_map[idx] = n;
			if (onAddOffset)
			{
				n->get_comp<Transform>()->pos = standBy[idx] + onAddOffset(n);
			}
			else {
				n->get_comp<Transform>()->pos = standBy[idx];
			}
			
			add_child(n);
		}

		int equator_count = 24;
		int level = 13;
		std::unordered_map < size_t , std::shared_ptr<Node<Component>>> pos_map;
		std::vector<glm::vec3> standBy;
		float off_angle = 0.01f;
		glm::vec3 pressed_pos;

	};
}