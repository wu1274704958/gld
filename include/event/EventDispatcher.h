#pragma once
#include "Event.h"
#include "EventHandler.h"
#include <sundry.hpp>
#include <node.hpp>
#include <component.h>
namespace evt {
	template<typename TarTy>
	struct EventDispatcher {

		constexpr static size_t ReserveCount = 5;
		
		using TargetTy = TarTy;

		EventDispatcher(glm::mat4& perspective, glm::mat4& world, int& w, int& h, glm::vec3 camera_pos,glm::vec3 &camera_dir, glm::vec3 view_pos) :
			perspective(perspective),
			world(world),
			w(w), h(h),camera_dir(camera_dir), camera_pos(camera_pos), view_pos(view_pos)
			{}

		void onMouseDown(int btn, int mode, int x, int y)
		{
			cache_btn = btn;

			MouseEvent<TargetTy> ce(EventType::MouseDown, btn);

			std::tuple<EventHandler<TargetTy>*, glm::vec3, float> res[ReserveCount];

			onMouse(x, y, ce,
				[&ce](EventHandler<TargetTy>* c)->bool {
				return c->handle_ty(EventType::MouseDown);
			},res,wws::arrLen(res));

			for (auto i = 0; i < wws::arrLen(res); ++i)
			{
				if (std::get<0>(res[i]))
				{
					ce.pos = std::get<1>(res[i]);
					ce.target = std::get<0>(res[i])->get_target();
					if (std::get<0>(res[i])->handle_event(&ce))
						break;
				}
			}
		}
		void onMouseUp(int btn, int mode, int x, int y)
		{
			MouseEvent<TargetTy> ce(EventType::MouseUp, btn);

			std::tuple<EventHandler<TargetTy>*, glm::vec3, float> res[ReserveCount];

			onMouse(x,y,ce,
				[&ce](EventHandler<TargetTy>* c)->bool {
				return c->handle_ty(EventType::MouseUp);
			},res,wws::arrLen(res));

			for (auto i = 0; i < wws::arrLen(res); ++i)
			{
				if (std::get<0>(res[i]))
				{
					int f = 0;
					ce.pos = std::get<1>(res[i]);
					bool launch_click = std::get<0>(res[i])->last_type == EventType::MouseDown;

					ce.target = std::get<0>(res[i])->get_target();
					ce.type = EventType::MouseUp;
					if (std::get<0>(res[i])->handle_event(&ce))
						++f;

					if (launch_click)
					{
						ce.type = EventType::Click;
						if (std::get<0>(res[i])->handle_event(&ce))
							++f;
					}
					if (f) break;
				}
			}
			cache_btn = -1;
		}
		void onMouseMove( int x, int y)
		{
			if (cache_btn >= 0)
			{
				MouseEvent<TargetTy> ce(EventType::MouseMove, cache_btn);

				std::tuple<EventHandler<TargetTy>*, glm::vec3, float> res[ReserveCount];

				onMouse(x,y,ce,
					[&ce](EventHandler<TargetTy>* c)->bool {
					return c->handle_ty(EventType::MouseMove) &&
						(c->last_type == EventType::MouseMove || c->last_type == EventType::MouseDown);
				},
					res,
					wws::arrLen(res),
					[&ce](EventHandler<TargetTy>* c) {
					ce.type = EventType::MouseOut;
					ce.target = c->get_target();
					c->handle_event(&ce);
				});
				
				for (auto i = 0; i < wws::arrLen(res); ++i)
				{
					if (std::get<0>(res[i]))
					{
						ce.pos = std::get<1>(res[i]);
						ce.type = EventType::MouseMove;
						ce.target = std::get<0>(res[i])->get_target();
						if (std::get<0>(res[i])->handle_event(&ce))
							break;
					}
				}
			}
		}

		void onMouse(int x,int y,MouseEvent<TargetTy>& ce,
			std::function<bool(EventHandler<TargetTy>*)> into,
			std::tuple<EventHandler<TargetTy>*,glm::vec3,float>* ehs,
			size_t len,
			std::function<void(EventHandler<TargetTy>*)> test_failed = std::function<void(EventHandler<TargetTy>*)>())
		{
			if (childlen_count() > 0)
			{
				for (auto i = 0; i < len; ++i)
				{
					std::get<0>(ehs[i]) = nullptr;
					std::get<1>(ehs[i]) = glm::vec3(0.f,0.f,0.f);
					std::get<2>(ehs[i]) = std::numeric_limits<float>::max();
				}

				glm::vec3 raypos, raydir;
				mouse_ray(x, y, raypos, raydir);
				
				ce.raypos = raypos; ce.raydir = raydir; ce.camera_pos = camera_pos; ce.world = world;
				ce.w_pos = glm::vec2(static_cast<float>(x), static_cast<float>(y));

				for (int i = 0; i < childlen_count(); ++i)
				{
					auto c = child(i);
					if (c && into(c))
					{
						if (c->onHandleMouseEvent(&ce) && glm::dot(glm::normalize(camera_dir), glm::normalize(ce.pos - camera_pos)) >= 0.f)
						{
							float dist = glm::length(ce.pos - camera_pos);
							for (auto j = 0; j < len; ++j)
							{
								if (dist < std::get<2>(ehs[j]))
								{
									if (j + 1 < len && std::get<0>(ehs[j]))
										ehs[j + 1] = ehs[j];
									std::get<1>(ehs[j]) = ce.pos;
									std::get<0>(ehs[j]) = c;
									std::get<2>(ehs[j]) = dist;
									break;
								}
							}
						}
						else {
							if(test_failed)
								test_failed(c);
						}
					}
				}
			}
		}

		void mouse_ray(int x, int y,glm::vec3& raypos,glm::vec3& raydir)
		{
			float nx, ny;
			sundry::screencoord_to_ndc(w, h, x, y, &nx, &ny);
			//dbg(std::make_tuple(nx, ny));

			auto t_world = glm::mat4(1.0f);
			t_world = glm::translate(t_world, view_pos);

			sundry::normalized2d_to_ray(nx, ny, glm::inverse(t_world * perspective), camera_pos, raypos, raydir);
		}

		glm::mat4& perspective, &world;
		int& w, & h;
		glm::vec3& camera_dir;
		glm::vec3 camera_pos, view_pos;
		std::function<int()> childlen_count;
		std::function<EventHandler<TargetTy>*(int)> child;
		int cache_btn;
	};
}


