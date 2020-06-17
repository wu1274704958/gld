#pragma once

#include "Event.h"


namespace evt {

	template<typename TarTy>
	struct EventHandler
	{
		using TargetTy = TarTy;
		using FUNC_TY = std::function<bool( Event<TargetTy>*)>;
		
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

			auto chs = evt_childlren();
			
			for (auto c : chs)
			{
				if (e->type == EventType::MouseDown ||
					e->type == EventType::MouseMove ||
					e->type == EventType::MouseUp ||
					e->type == EventType::MouseOut ||
					e->type == EventType::Click
					)
				{
					MouseEvent<TargetTy> ce = *reinterpret_cast<MouseEvent<TargetTy>*>(e);
					if (c->onHandleMouseEvent(&ce))
					{
						ce.target = c->get_target();
						if (c->handle_event(e))
							return true;
					}
				}
			}

			if (has_listener(e->type))
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

		virtual std::vector<EventHandler<TargetTy>*> evt_childlren() = 0;
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