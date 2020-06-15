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
	};

	template<typename TarTy>
	struct MouseEvent : public Event<TarTy>
	{
		using TargetTy = TarTy;
		EventType type;
		std::weak_ptr<TargetTy> target;
		glm::vec3 pos;
		int btn;

		MouseEvent(EventType type, std::weak_ptr<TargetTy> target,glm::vec3& pos,int btn) : Event<TargetTy>(type,std::move(target)) ,
			pos(pos),btn(btn)
		{

		}
	};

	

}