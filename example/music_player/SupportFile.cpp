#include "SupportFile.h"
#include <cstring>
#ifdef PF_WIN32
#include <Windows.h>
#else
#include "MMFile.h"
#endif

const MMFILE_CHAR_TYPE  * const SupportFile::supportList[SupportFile::supportNum] = {
#ifdef PF_WIN32
	L".mp3",
	L".flac",
	L".wav"
#else
 	".mp3",
	".flac",
	".wav"
#endif
};

bool SupportFile::isSupport(const MMFILE_CHAR_TYPE *other)
{
	for (int i = 0; i < supportNum; i++)
	{
		if (lstrcmpW(supportList[i],other) == 0)
			return true;
	}
	return false;
	
}
