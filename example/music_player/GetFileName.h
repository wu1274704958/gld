//
// Created by wws on 17-7-19.
//

#ifndef FIRST_GETFILENAME_H
#define FIRST_GETFILENAME_H

#include <iostream>
#include <vector>
#include "MMFile.h"


class GetFileName { 

public:
    static void getFileNameA(std::vector<MMFile> &v, const MMFILE_CHAR_TYPE *d);
	static void getFileNameW(std::vector<MMFile> &v, const MMFILE_CHAR_TYPE *d);
};


#endif //FIRST_GETFILENAME_H
