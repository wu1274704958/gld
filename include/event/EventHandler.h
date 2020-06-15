#pragma once

#include "Event.h"

namespace evt {

	template<typename TarTy>
	struct EventHandler
	{
		using TargetTy = TarTy;
		
		EventType last_type = EventType::None;
		unsigned long long handle_type = std::numeric_limits<unsigned long long>::max();
		std::unordered_map<EventType, std::function<Event<TargetTy>*>> handle_func;
	};
}