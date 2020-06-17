#pragma once
#include "Event.h"
#include "EventHandler.h"
#include <sundry.hpp>
#include <node.hpp>
#include <component.h>
namespace evt {
	template<typename TarTy>
	struct EventDispatcher {
		
		using TargetTy = TarTy;

		EventDispatcher(glm::mat4& perspective, glm::mat4& world, int& w, int& h, glm::vec3 camera_pos, glm::vec3 view_pos) :
			perspective(perspective),
			world(world),
			w(w), h(h), camera_pos(camera_pos), view_pos(view_pos)
			{}

		void onMouseDown(int btn, int mode, int x, int y)
		{
			cache_btn = btn;
			if (childlen_count() > 0)
			{
				glm::vec3 raypos, raydir;
				mouse_ray(x, y, raypos, raydir);
				for (int i = 0;i < childlen_count();++i)
				{
					auto c = child(i);
					if (c && c->handle_ty(EventType::MouseDown))
					{
						MouseEvent<TargetTy> ce(EventType::MouseDown, btn);
						ce.raypos = raypos; ce.raydir = raydir; ce.camera_pos = camera_pos; ce.world = world;
						ce.w_pos = glm::vec2(static_cast<float>(x), static_cast<float>(y));
						if (c->onHandleMouseEvent(&ce))
						{
							ce.target = c->get_target();
							c->handle_event(&ce);
						}
					}
				}
			}
		}
		void onMouseUp(int btn, int mode, int x, int y)
		{
			if (childlen_count() > 0)
			{
				glm::vec3 raypos, raydir;
				mouse_ray(x, y, raypos, raydir);
				for (int i = 0; i < childlen_count(); ++i)
				{
					auto c = child(i);
					if (c && c->handle_ty(EventType::MouseUp))
					{
						MouseEvent<TargetTy> ce(EventType::MouseUp, btn);
						ce.raypos = raypos; ce.raydir = raydir; ce.camera_pos = camera_pos; ce.world = world;
						ce.w_pos = glm::vec2(static_cast<float>(x), static_cast<float>(y));
						if (c->onHandleMouseEvent(&ce))
						{
							bool launch_click = c->last_type == EventType::MouseDown;

							ce.target = c->get_target();
							c->handle_event(&ce);

							if (launch_click)
							{
								ce.type = EventType::Click;
								c->handle_event(&ce);
							}
						}
					}
				}
			}
			cache_btn = -1;
		}
		void onMouseMove( int x, int y)
		{
			if (cache_btn >= 0)
			{
				if (childlen_count() > 0)
				{
					glm::vec3 raypos, raydir;
					mouse_ray(x, y, raypos, raydir);
					for (int i = 0; i < childlen_count(); ++i)
					{
						auto c = child(i);
						if (c && c->handle_ty(EventType::MouseMove))
						{
							MouseEvent<TargetTy> ce(EventType::MouseMove, cache_btn);
							ce.raypos = raypos; ce.raydir = raydir; ce.camera_pos = camera_pos; ce.world = world;
							ce.w_pos = glm::vec2(static_cast<float>(x), static_cast<float>(y));
							if (c->onHandleMouseEvent(&ce))
							{
								ce.target = c->get_target();
								c->handle_event(&ce);
							}
							else {
								if (c->last_type == EventType::MouseMove || c->last_type == EventType::MouseDown)
								{
									ce.type = EventType::MouseOut;
									ce.target = c->get_target();
									c->handle_event(&ce);
								}
							}
						}
					}
				}
			}
		}

		void mouse_ray(int x, int y,glm::vec3& raypos,glm::vec3& raydir)
		{
			float nx, ny;
			sundry::screencoord_to_ndc(w, h, x, y, &nx, &ny);
			dbg(std::make_tuple(nx, ny));

			auto t_world = glm::mat4(1.0f);
			t_world = glm::translate(t_world, view_pos);

			sundry::normalized2d_to_ray(nx, ny, glm::inverse(t_world * perspective), camera_pos, raypos, raydir);
		}

		glm::mat4& perspective, &world;
		glm::vec3 camera_pos,view_pos;
		int& w, &h; 
		std::function<int()> childlen_count;
		std::function<EventHandler<TargetTy>*(int)> child;
		int cache_btn;
	};
}


