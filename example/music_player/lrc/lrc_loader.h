#pragma once

#include "comm.h"
#include <optional>

namespace lrc {

	

	struct LrcLoader
	{
		using LrcType = Lrc;
		std::optional<const LrcType&> load(const std::string& path);
		std::unordered_map<std::string, LrcType> map;
	};


}