#pragma once

#include "Event.h"
#include <functional>

namespace evt {

	template<typename TarTy>
	struct EventHandler
	{
		using TargetTy = TarTy;
		using FUNC_TY = std::function<bool( Event<TargetTy>*)>;

		constexpr static size_t ReserveCount = 5;
		
		EventType last_type = EventType::None;


		void add_listener(EventType ty, FUNC_TY f)
		{
			handle_func[ty] = f;
		}

		FUNC_TY get_listener(EventType ty)
		{
			return handle_func[ty];
		}

		bool has_listener(EventType ty)
		{
			return handle_func.find(ty) != handle_func.end();
		}

		bool handle_ty(EventType ty)
		{
			return (handle_type & static_cast<unsigned long long>(ty)) == static_cast<unsigned long long>(ty);
		}

		void set_handle_type(EventType ty)
		{
			handle_type = static_cast<unsigned long long>(ty);
		}

		bool handle_event(Event<TargetTy>*e)
		{
			last_type = e->type;

			if (evt_children_count() > 0)
			{
				if (e->type == EventType::MouseDown ||
					e->type == EventType::MouseMove ||
					e->type == EventType::MouseUp ||
					e->type == EventType::MouseOut ||
					e->type == EventType::Click
					)
				{
					MouseEvent<TargetTy> ce = *reinterpret_cast<MouseEvent<TargetTy>*>(e);
					std::tuple<EventHandler<TargetTy>*, glm::vec3, float> ehs[ReserveCount];
					for (auto i = 0; i < wws::arrLen(ehs); ++i)
					{
						std::get<0>(ehs[i]) = nullptr;
						std::get<1>(ehs[i]) = glm::vec3(0.f, 0.f, 0.f);
						std::get<2>(ehs[i]) = std::numeric_limits<float>::max();
					}

					for (int i = 0; i < evt_children_count(); ++i)
					{
						auto c = evt_child(i);
						if (!c) continue;
						if (c->onHandleMouseEvent(&ce))
						{
							float dist = glm::length(ce.pos - ce.camera_pos);
							for (auto j = 0; j < wws::arrLen(ehs); ++j)
							{
								if (dist < std::get<2>(ehs[j]))
								{
									if (j + 1 < wws::arrLen(ehs) && std::get<0>(ehs[j]))
										ehs[j + 1] = ehs[j];
									std::get<1>(ehs[j]) = ce.pos;
									std::get<0>(ehs[j]) = c;
									std::get<2>(ehs[j]) = dist;
									break;
								}
							}
						}
					}
					for (auto i = 0; i < wws::arrLen(ehs); ++i)
					{
						if (std::get<0>(ehs[i]))
						{
							ce.pos = std::get<1>(ehs[i]);
							ce.target = std::get<0>(ehs[i])->get_target();
							if (std::get<0>(ehs[i])->handle_event(&ce))
								return true;
						}
						else {
							break;
						}
					}
				}
			}

			if (has_listener(e->type) && handle_func[e->type])
			{
				return (handle_func[e->type])(e);
			}


			switch (e->type)
			{
			case EventType::MouseDown:
				return onMouseDown(reinterpret_cast<MouseEvent<TargetTy>*>(e));
				break;
			case EventType::MouseUp:
				return onMouseUp(reinterpret_cast<MouseEvent<TargetTy>*>(e));
				break;
			case EventType::MouseOut:
				return onMouseOut(reinterpret_cast<MouseEvent<TargetTy>*>(e));
				break;
			case EventType::MouseMove:
				return onMouseMove(reinterpret_cast<MouseEvent<TargetTy>*>(e));
				break;
			case EventType::Click:
				return onClick(reinterpret_cast<MouseEvent<TargetTy>*>(e));
				break;
			}
			return false;
		}

		virtual int evt_children_count() = 0;
		virtual EventHandler<TargetTy>* evt_child(int) = 0;
		virtual bool onMouseDown(MouseEvent<TargetTy>*) { return false; }
		virtual bool onMouseMove(MouseEvent<TargetTy>*) { return false; }
		virtual bool onMouseUp( MouseEvent<TargetTy>*) { return false; }
		virtual bool onMouseOut( MouseEvent<TargetTy>*) { return false; }
		virtual bool onClick( MouseEvent<TargetTy>*) { return false; }
		virtual bool onHandleMouseEvent(MouseEvent<TargetTy>*) = 0;
		virtual std::weak_ptr<TargetTy> get_target() = 0;

		unsigned long long handle_type = 
				static_cast<unsigned long long>(EventType::Click) | 
				static_cast<unsigned long long>(EventType::MouseDown) | 
				static_cast<unsigned long long>(EventType::MouseUp);
	protected:
		
		std::unordered_map<EventType, FUNC_TY> handle_func;
		
	};
}