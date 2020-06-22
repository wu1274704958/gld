#pragma once
#include <string>

namespace cvt {

	std::string unicode2utf8(const std::wstring&);

	std::wstring utf82unicode(const std::string&);

	std::string unicode2ansi(const std::wstring&);

	std::wstring ansi2unicode(const std::string&);

	std::string utf8_l(const std::string&);
	
	std::string l_utf8(const std::string&);

}