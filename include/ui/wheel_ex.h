#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <generator/Generator.hpp>
#include <tools/app.h>
#include <tools/tween.hpp>

namespace gld {

	struct WheelEx {

		WheelEx(){}
		WheelEx(int equator_count,float radius,size_t size) : 
			equator_count(equator_count),
			radius(radius),
			max_idx(size - 1),
			entity_num(equator_count + 1) {}

		void create()
		{
			assert((equator_count % 2) == 1);
			assert(count() >= equator_count);
			fake_end = fake_begin + equator_count - 1;
			glm::mat4 m(1.f);
			m = glm::rotate(m, glm::radians(rotate), rotate_dir);
			
			slot_rotate_dir = glm::vec3(m * glm::vec4(slot_rotate_dir.x,slot_rotate_dir.y,slot_rotate_dir.z,1.f));

			auto res = gen::circle(0.f, radius, equator_count + 2 + unless);

			for (int i = 0;i < equator_count + 2;++i)
			{
				auto& v = res[i];
				standBy.push_back(m * glm::vec4(v.x,v.y,v.z,1.f));
			}
			angle = glm::acos(glm::dot(glm::normalize(res[0]), glm::normalize(res[1])));

			glm::mat4 m2(1.f);

			m2 = glm::rotate(m2, -angle * (equator_count / 2 + 1), slot_rotate_dir);
			for (auto& v : standBy)
			{
				v = (m2 * glm::vec4(v.x,v.y,v.z,1.f));
				standBy_origin.push_back(v);
			}

			adjust_pos_map();
			set_slot_rotate();
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
				float b = (float)iidx_for_idx(curr) * -angle;
				float e = (float)iidx_for_idx(i) * -angle;
				int intent = i > curr ? 1 : -1;
				adjust_pos_map(intent);
				App::instance()->tween.to(
					[this](float v) {
						slot_rotate = v;
						set_slot_rotate();
					}, dur, b, e, f, [this,i,intent]() {	
						if(on_select)on_select(i);
						curr = i;
						adjust_pos_map();
						origin();
						set_slot_rotate();
				});
			}
		}

		bool good(int i)
		{
			return i <= max_idx && i >= min_idx;
		}

		int count()
		{
			return max_idx - min_idx + 1;
		}

		void set_slot_rotate()
		{
			if(!on_update_pos) return;
			glm::mat4 matrix(1.f);

			matrix = glm::translate(matrix, pos);

			matrix = glm::rotate(matrix, slot_rotate, slot_rotate_dir);

			size_t i = fake_begin;
			size_t idx = 0;
			for (int k = 0;k < entity_num;++k)
			{
				int v = k + 1;
				if(k == entity_num - 1 && !behind) v = 0; 
				bool is_none = (i < min_idx || i > max_idx);

				if (onAddOffset)
				{
					auto t = (standBy[v] + onAddOffset(v));
					on_update_pos(idx,i,matrix * glm::vec4(t.x,t.y,t.z,1.f),is_none);
				}
				else {
					on_update_pos(idx,i,matrix * glm::vec4(standBy[v].x, standBy[v].y, standBy[v].z, 1.f),is_none);
				}
				++i;++idx;
			}
		}

		void adjust_pos_map(int intent = 1)
		{
			pos_map.clear();
			fake_begin = curr - side();
			fake_end = fake_begin + equator_count - 1;

			if(fake_begin <= min_idx)
				intent = 1;
			if(fake_end >= max_idx)
				intent = -1;
			behind = intent == 1;
		}

		void origin()
		{
			standBy = standBy_origin;
			slot_rotate = 0.f;
		}

		size_t iidx_for_idx(size_t i)
		{
			return side() - (curr - i);
		}

		size_t idx_for_iidx(size_t i)
		{
			return curr - (side() - i);
		}

		int side()
		{
			return equator_count / 2;
		}

		std::function<void(int)> on_select;

		float rotate = 0.f;
		glm::vec3 rotate_dir = glm::vec3(0.f,0.f,1.f);
		float slot_rotate = 0.f;
		float slot_rotate_rate = 0.007f;
		glm::vec3 pos = glm::vec3(0.f, 0.f, 0.f);
		std::function<glm::vec3(size_t)> onAddOffset;
		std::function<void(size_t,size_t,glm::vec3,bool)> on_update_pos;
		size_t min_idx = 0,max_idx = 0;
		int unless = 0;
	protected:

		float angle,radius = 1.f;
		size_t curr = 0,curr_fake = 0,entity_num = 0,fake_begin = 0,fake_end = 0;
		size_t equator_count = 6;
		bool behind = true;
		std::vector<size_t> pos_map;
		std::vector<glm::vec3> standBy;
		std::vector<glm::vec3> standBy_origin;
		glm::vec3 slot_rotate_dir = glm::vec3(0.f,1.0f,0.f);
	};
}