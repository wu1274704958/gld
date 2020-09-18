//
// Created by wws on 17-7-19.
//

#include "GetFileName.h"
#ifdef PF_WIN32
#include <Windows.h>
#endif
#include "SupportFile.h"
#ifndef PF_WIN32
#include <filesystem>
namespace fs = std::filesystem;
#endif

#ifdef PF_WIN32
void GetFileName::getFileNameA(std::vector<MMFile> &v, const char *pChar)
{
	if (!pChar) return ;
	wchar_t *pszBuf = NULL;
	int needWChar = MultiByteToWideChar(CP_ACP, 0, pChar, -1, NULL, 0);
	if (needWChar > 0) {
		pszBuf = new wchar_t[needWChar + 1];
		ZeroMemory(pszBuf, (needWChar + 1) * sizeof(wchar_t));
		MultiByteToWideChar(CP_ACP, 0, pChar, -1, pszBuf, needWChar);
	}

	getFileNameW(v, pszBuf);
    
	if (pszBuf)
		delete[] pszBuf;
}

void GetFileName::getFileNameW(std::vector<MMFile>& v, const wchar_t * d)
{
	WIN32_FIND_DATAW ffd;
	HANDLE han = nullptr;

	std::wstring dir(d);

	dir += L"\\*";

	if ((han = FindFirstFileW(dir.c_str(), &ffd)) != nullptr)
	{
		while (FindNextFileW(han, &ffd))
		{
			if (lstrcmpW(ffd.cFileName, L".") == 0 || lstrcmpW(ffd.cFileName, L"..") == 0)
				continue;
			//std::wcout << ffd.dwFileAttributes << "  " << ffd.cFileName << std::endl;
			MMFile tempFile(ffd.dwFileAttributes == 16 ? MMFile::TYPE_DIR : MMFile::TYPE_FILE, ffd.cFileName, d);
			if (SupportFile::isSupport(tempFile.getSuffix()) || tempFile.getType() == MMFile::TYPE_DIR)
				v.push_back(std::move(tempFile));
		}
	}

}
#else

void GetFileName::getFileNameA(std::vector<MMFile> &v, const MMFILE_CHAR_TYPE *pChar)
{
	fs::path root(pChar);
	for (auto& it : std::filesystem::directory_iterator(root))
	{
		if(it.is_directory())
		{
			auto p = it.path();
			v.push_back( MMFile(MMFile::TYPE_DIR,p.filename().generic_u8string().c_str(),p.parent_path().generic_u8string().c_str()) );
		}else if(it.is_regular_file()){
			auto p = it.path();
			auto suffix = p.extension().generic_u8string();
			if(SupportFile::isSupport(suffix.c_str()))
			{
				v.push_back( MMFile(MMFile::TYPE_FILE,p.filename().generic_u8string().c_str(),p.parent_path().generic_u8string().c_str()) );
			}
		}
	}
}

void GetFileName::getFileNameW(std::vector<MMFile>& v, const MMFILE_CHAR_TYPE * d)
{
	getFileNameA(v,d);
}

#endif