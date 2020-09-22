#pragma once

namespace gld {

	struct Wheel {

		Wheel(){}
		Wheel(int equator_count,float radius) : equator_count(equator_count),radius(radius) {}

		void create()
		{
			glm::mat4 m(1.f);
			m = glm::rotate(m, glm::radians(rotate), rotate_dir);
			
			slot_rotate_dir = glm::vec3(m * glm::vec4(slot_rotate_dir.x,slot_rotate_dir.y,slot_rotate_dir.z,1.f));

			auto res = gen::circle(0.f, radius, equator_count);

			for (auto& v : res)
			{
				standBy.push_back(m * glm::vec4(v.x,v.y,v.z,1.f));
			}
			angle = glm::acos(glm::dot(glm::normalize(res[0]), glm::normalize(res[1])));

		}
		
		int get_curr()
		{
			return curr;
		}

		float get_angle()
		{
			return angle;
		}

		void tween_to(int i,float dur,Tween::TweenFuncTy f = tween::Circ::easeOut)
		{
			if (i != curr && good(i))
			{
				float b = (float)curr * -angle;
				float e = (float)i * -angle;
				App::instance()->tween.to(
					[this](float v) {
						slot_rotate = v;
						set_slot_rotate();
					}, dur, b, e, f, [this,i]() {	
						if(on_select)on_select(i);
						curr = i;
				});
			}
		}

		bool good(int i)
		{
			return i < standBy.size();
		}

		bool full()
		{
			return standBy.size() == pos_map.size();
		}

		bool add(size_t i,std::shared_ptr<Node<Component>> n)
		{
			if (good(i))
			{
				_add(i, n);
			}
			return false;
		}

		int count()
		{
			return pos_map.size();
		}

		bool has(size_t idx)
		{
			return pos_map.find(idx) != pos_map.end();
		}

		bool remove(size_t i)
		{
			if (has(i))
			{
				pos_map.erase(i);
				return true;
			}
			return false;
		}

		void set_slot_rotate()
		{
			glm::mat4 matrix(1.f);

			matrix = glm::translate(matrix, pos);

			matrix = glm::rotate(matrix, slot_rotate, slot_rotate_dir);

			for (auto& v : pos_map)
			{
				if (onAddOffset)
				{
					auto t = (standBy[v.first] + onAddOffset(v.second));
					v.second->get_comp<Transform>()->pos = matrix * glm::vec4(t.x,t.y,t.z,1.f);
				}
				else {
					v.second->get_comp<Transform>()->pos = matrix * glm::vec4(standBy[v.first].x, standBy[v.first].y, standBy[v.first].z, 1.f);
				}
			}
		}

		std::function<void(int)> on_select;

		float rotate = 0.f;
		glm::vec3 rotate_dir = glm::vec3(0.f,0.f,1.f);
		float slot_rotate = 0.f;
		float slot_rotate_rate = 0.007f;
		glm::vec3 pos = glm::vec3(0.f, 0.f, 0.f);
		std::function<glm::vec3(const std::shared_ptr<Node<Component>>&)> onAddOffset;
	protected:
		void _add(size_t idx, std::shared_ptr<Node<Component>> n)
		{
			pos_map[idx] = n;
			glm::mat4 matrix(1.f);

			matrix = glm::translate(matrix, pos);

			matrix = glm::rotate(matrix, slot_rotate, slot_rotate_dir);

			if (onAddOffset)
			{
				auto t = (standBy[idx] + onAddOffset(n));
				n->get_comp<Transform>()->pos = matrix * glm::vec4(t.x, t.y, t.z, 1.f);
			}
			else {
				n->get_comp<Transform>()->pos = matrix * glm::vec4(standBy[idx].x, standBy[idx].y, standBy[idx].z, 1.f);
			}
		}

		float angle,radius = 1.f;
		int curr = 0;
		int equator_count = 6;
		std::unordered_map < size_t, std::shared_ptr<Node<Component>>> pos_map;
		std::vector<glm::vec3> standBy;
		glm::vec3 slot_rotate_dir = glm::vec3(0.f,1.0f,0.f);
	};
}