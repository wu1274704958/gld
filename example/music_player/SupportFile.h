#pragma once

#include "MMFile.h"

class SupportFile {
	static const int supportNum = 3;
	static const  MMFILE_CHAR_TYPE * const supportList[supportNum];
	
public:
	static bool isSupport(const MMFILE_CHAR_TYPE *);
};