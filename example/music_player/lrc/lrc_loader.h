#pragma once

#include "comm.h"
#include <optional>
#include <map>

namespace lrc {

	

	struct LrcLoader
	{
		using LrcType = Lrc;
		std::shared_ptr<LrcType> load(const std::string& path);
		std::map<std::string, std::shared_ptr<LrcType>> map;
		double parse_time(const std::string &s);
	};


}