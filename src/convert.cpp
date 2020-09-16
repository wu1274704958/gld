#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include "tools/convert.h"
#include <locale>
#include <codecvt>

#ifndef PF_ANDROID
#ifdef PF_WIN32
#include <Windows.h>
#endif //PF_Win32
#endif // PF_ANDROID


#include <iostream>

namespace cvt {

	std::string unicode2utf8(const std::wstring& ws)
	{
		std::string ret;
		try {
			std::wstring_convert< std::codecvt_utf8<wchar_t> > wcv;
			ret = wcv.to_bytes(ws);
		}
		catch (const std::exception & e) {
			std::cerr << e.what() << std::endl;
		}
		return ret;
	}

	std::wstring utf82unicode(const std::string& s)
	{
		std::wstring ret;
		try {
			std::wstring_convert< std::codecvt_utf8<wchar_t> > wcv;
			ret = wcv.from_bytes(s);
		}
		catch (const std::exception & e) {
			std::cerr << e.what() << std::endl;
		}
		return ret;
	}

#ifndef PF_ANDROID
#ifdef PF_WIN32
	std::string unicode2ansi(const std::wstring& ws)
	{
		int ansiiLen = ::WideCharToMultiByte(CP_ACP, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
		char *pAssii = (char*)malloc(sizeof(char)*ansiiLen);
		::WideCharToMultiByte(CP_ACP, 0, ws.c_str(), -1, pAssii, ansiiLen, nullptr, nullptr);
		std::string ret_str = pAssii;
		free(pAssii);
		return ret_str;
	}

	std::wstring ansi2unicode(const std::string& s)
	{
		int unicodeLen = ::MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
		wchar_t *pUnicode = (wchar_t*)malloc(sizeof(wchar_t)*unicodeLen);
		::MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, pUnicode, unicodeLen);
		std::wstring ret_str = pUnicode;
		free(pUnicode);
		return ret_str;
	}


	std::string utf8_l(const std::string& str)
	{
		return unicode2ansi(utf82unicode(str));
	}

	std::string l_utf8(const std::string& str)
	{
		return unicode2utf8(ansi2unicode(str));
	}
#else
	std::string unicode2ansi(const std::wstring& ws)
	{
		return unicode2utf8(ws);
	}

	std::wstring ansi2unicode(const std::string& s)
	{
		return utf82unicode(s);
	}
#endif
#else
#ifndef PF_WIN32

	std::string unicode2ansi(const std::wstring& ws)
	{
		return unicode2utf8(ws);
	}

	std::wstring ansi2unicode(const std::string& s)
	{
		return utf82unicode(s);
	}
#endif
#endif
}