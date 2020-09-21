#pragma once
#include <vector>
#include <string>
namespace lrc {
	struct Line
	{
		Line(double sec,const std::string& line)
		{
			this->second = sec;
			this->line = line;
		}
		double second;
		std::string line;
	};
	struct Lrc
	{
		std::vector<Line> data;
	};
}