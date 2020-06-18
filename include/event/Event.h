#pragma once

#include <memory>
#include <glm/glm.hpp>

namespace evt {

	enum class EventType : unsigned long long
	{
		None = 0x0,
		MouseDown = 0x1,
		MouseMove = 0x2,
		MouseUp   = 0x4,
		MouseOut  = 0x8,
		Click	  = 0x16
	};

	template<typename TarTy>
	struct Event 
	{
		using TargetTy = TarTy;
		EventType type;
		std::weak_ptr<TargetTy> target;

		Event(EventType type, std::weak_ptr<TargetTy> target) : type(type), target(std::move(target))
		{

		}

		Event(EventType type) : type(type)
		{

		}
	};

	template<typename TarTy>
	struct MouseEvent : public Event<TarTy>
	{
		using TargetTy = TarTy;
		glm::mat4 world;
		glm::vec3 raypos, raydir,camera_pos,model_pos;
		glm::vec3 pos;
		glm::vec2 w_pos;
		int btn;

		MouseEvent(EventType type, std::weak_ptr<TargetTy> target,glm::vec3& pos,int btn) : Event<TargetTy>(type,std::move(target)) ,
			pos(pos),btn(btn)
		{

		}

		MouseEvent(EventType type,int btn) : Event<TargetTy>(type), btn(btn)
		{

		}

	};

	
    template<EventType ty, typename T>
    struct EventUnitTy
    {
		static_assert(std::is_base_of_v<Event, T>,"This type must inherited from Event!");
        constexpr static size_t event_type = static_cast<size_t>(ty);
        using type = T;
    };

    template<size_t Rt, typename ...Ts>
    struct MapEventTy;

    template<size_t Rt, typename Fir, typename ...Ts>
    struct MapEventTy<Rt, Fir, Ts...>
    {
        constexpr static decltype(auto) func()
        {
            if constexpr (Rt == Fir::event_type)
            {
                using T = typename Fir::type;
                return std::declval<T>();
            }
            else
            {
                using T = typename MapEventTy<Rt, Ts...>::type;
                if constexpr (std::is_same_v<T, void>)
                {
                    static_assert("Error Type!!!");
                }
                return std::declval<T>();
            }
        }
        using type = typename std::remove_reference_t<decltype(func())>;
    };

	template<size_t Rt>
	struct MapEventTy<Rt>
	{
		using type = void;
	};

	template<EventType t,typename Tar>
	using map_event_t = typename MapEventTy<static_cast<size_t>(t),
		EventUnitTy<EventType::None, Event<Tar>>,
		EventUnitTy<EventType::MouseDown, MouseEvent<Tar>>,
		EventUnitTy<EventType::MouseMove, MouseEvent<Tar>>,
		EventUnitTy<EventType::MouseUp, MouseEvent<Tar>>,
		EventUnitTy<EventType::MouseOut, MouseEvent<Tar>>,
		EventUnitTy<EventType::Click, MouseEvent<Tar>>>::type;
}