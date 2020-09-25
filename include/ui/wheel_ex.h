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

		void set_curr(int v)
		{
			curr = v;
		}
		
		int get_curr()
		{
			return curr;
		}

		float get_angle()
		{
			return angle;
		}

		void tween_next(float dur,Tween::TweenFuncTy f = tween::Circ::easeOut)
		{
			if(curr + 1 <= max_idx)
				tween_to_(curr + 1,dur,f);
		}

		void tween_last(float dur,Tween::TweenFuncTy f = tween::Circ::easeOut)
		{
			if(curr - 1 >= min_idx)
				tween_to_(curr - 1,dur,f);
		}

		void tween_to(int i,float dur,Tween::TweenFuncTy f = tween::Circ::easeOut)
		{
			if(good(i) && curr != i)
			{
				int n = i > curr ? curr + 1 : curr - 1;
				if (abs(curr - i) == 1)
				{
					i > curr ? tween_next(dur,f) : tween_last(dur, f);
					return;
				}
				tween_to_(n,dur,f,[this,i,dur,f]()
				{
					//printf("%d %d\n",i,curr);
					tween_to(i,dur,f);
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

		void refresh()
		{
			adjust_pos_map();
			set_slot_rotate();
		}

		void  set_count(int v)
		{
			min_idx = 0;
			max_idx = v - 1;
			curr = 0;
		}

		glm::vec3 get_pos(int i)
		{
			return standBy[i];
		}

		void set_slot_rotate()
		{
			if(!on_update_pos) return;
			glm::mat4 matrix(1.f);

			matrix = glm::translate(matrix, pos);

			matrix = glm::rotate(matrix, slot_rotate, slot_rotate_dir);

			size_t i = fake_begin;
			size_t idx = 0;
			int k = 0;
			if(!behind) 
			{
				k = entity_num - 1;
				i -= 1;
			}
			for (;;)
			{
				int v = k + 1;
				if(k == entity_num - 1 && !behind) v = 0; 
				bool is_none = !good(i);
				if (onAddOffset)
				{
					auto t = (standBy[v] + onAddOffset(v));
					auto pos = matrix * glm::vec4(t.x,t.y,t.z,1.f);
					float rotate = glm::acos( glm::dot( glm::normalize(glm::vec3(pos)),glm::normalize( standBy_origin[side() + 1])));
					if(v > side() + 1) rotate = -rotate;
					on_update_pos(idx,i,pos,rotate,is_none);
				}
				else {
					auto pos = matrix * glm::vec4(standBy[v].x, standBy[v].y, standBy[v].z, 1.f);
					float rotate = glm::acos( glm::dot( glm::normalize(glm::vec3(pos)),glm::normalize( standBy_origin[side() + 1])));
					if(v > side() + 1) rotate = -rotate;
					on_update_pos(idx,i,pos,rotate,is_none);
				}
				++i;++idx;
				if(behind && k == entity_num - 1)
					break;
				if(!behind && k == entity_num - 2)
					break;
				if(!behind)
				{
					if(k == entity_num - 1)
						k = 0;
					else
					 	++k;
				}
				if(behind) ++k;
			}
		}

		void adjust_pos_map(int intent = 1)
		{
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

		int iidx_for_idx(int i)
		{
			return side() - (curr - i);
		}

		int idx_for_iidx(int i)
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
		std::function<glm::vec3(int)> onAddOffset;
		std::function<void(int,int,glm::vec3,float,bool)> on_update_pos;
		int min_idx = 0,max_idx = 0;
		int unless = 0;
	protected:
		bool is_scrolling = false;
		void tween_to_(int i,float dur,Tween::TweenFuncTy f = tween::Circ::easeOut,
			std::function<void()> complete = nullptr)
		{
			if (!is_scrolling && i != curr && good(i))
			{
				is_scrolling = true;
				float b = (float)iidx_for_idx(curr - side()) * -angle;
				float e = (float)iidx_for_idx(i - side())* -angle;
				int intent = i > curr ? 1 : -1;
				adjust_pos_map(intent);
				set_slot_rotate();
				App::instance()->tween.to(
					[this](float v) {
						slot_rotate = v;
						set_slot_rotate();
					}, dur, b, e, f, [this,i,intent,complete]() {	
						if(on_select)on_select(i);
						curr = i;
						adjust_pos_map();
						origin();
						set_slot_rotate();
						if(complete) 
						{
							App::instance()->exec.delay_frame(complete,1);
						}
						is_scrolling = false;
				});
			}
		}

		float angle,radius = 1.f;
		int curr = 0,curr_fake = 0,entity_num = 0,fake_begin = 0,fake_end = 0;
		int equator_count = 6;
		bool behind = true;
		std::vector<glm::vec3> standBy;
		std::vector<glm::vec3> standBy_origin;
		glm::vec3 slot_rotate_dir = glm::vec3(0.f,1.0f,0.f);
	};
}