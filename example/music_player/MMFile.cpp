//
// Created by wws on 17-7-19.
//

#include "MMFile.h"
#include <memory>

MMFile::MMFile()
{
    this->type = TYPE_FILE;
    this->name = nullptr;
    this->path = nullptr;
    this->suffix = nullptr;
    this->absolutePath = nullptr;
}

MMFile::MMFile(FILE_TYPE type,const MMFILE_CHAR_TYPE *name, const MMFILE_CHAR_TYPE *path)
{
    this->type = type;
    this->name = new MMFILE_STR_TYPE(name);
    this->path = new MMFILE_STR_TYPE(path);
    analysisSuffix();
    analysisAbsolutePath();
}

MMFile::MMFile(const MMFile &mmf)
{
    this->type = mmf.type;
	if (mmf.name)
		this->name = new MMFILE_STR_TYPE(*(mmf.name));
	if (mmf.path)
		this->path = new MMFILE_STR_TYPE(*(mmf.path));
    if(mmf.suffix != nullptr)
        this->suffix = new MMFILE_STR_TYPE(*(mmf.suffix));
    else
        analysisSuffix();
    if(mmf.absolutePath != nullptr)
        this->absolutePath = new MMFILE_STR_TYPE(*(mmf.absolutePath));
    else
        analysisAbsolutePath();
}

MMFile::MMFile(MMFile &&mmf)
{
    this->type = mmf.type;

    this->name = mmf.name;
    this->path = mmf.path;
    this->suffix = mmf.suffix;
    this->absolutePath = mmf.absolutePath;
    mmf.name = nullptr;
    mmf.path = nullptr;
    mmf.suffix = nullptr;
    mmf.absolutePath = nullptr;
}

MMFile & MMFile::operator=(const MMFile & mmf)
{
	cleanup();
	this->type = mmf.type;
	this->type = mmf.type;
	if (mmf.name)
		this->name = new MMFILE_STR_TYPE(*(mmf.name));
	if (mmf.path)
		this->path = new MMFILE_STR_TYPE(*(mmf.path));
	if (mmf.suffix != nullptr)
		this->suffix = new MMFILE_STR_TYPE(*(mmf.suffix));
	else
		analysisSuffix();
	if (mmf.absolutePath != nullptr)
		this->absolutePath = new MMFILE_STR_TYPE(*(mmf.absolutePath));
	else
		analysisAbsolutePath();

	return *this;
}

MMFile & MMFile::operator=(MMFile && mmf)
{
#ifdef _DEBUG
	std::cout << "operator=(MMFile && mmf) " << this->name << std::endl;
#endif // DEBUG
	cleanup();
	this->type = mmf.type;

	this->name = mmf.name;
	this->path = mmf.path;
	this->suffix = mmf.suffix;
	this->absolutePath = mmf.absolutePath;
	mmf.name = nullptr;
	mmf.path = nullptr;
	mmf.suffix = nullptr;
	mmf.absolutePath = nullptr;

	return *this;
}

MMFile::~MMFile()
{
#ifdef _DEBUG
	std::cout << "~MMFile() " <<(size_t)(this->name) << std::endl;
#endif // DEBUG
	cleanup();
}

MMFile::FILE_TYPE MMFile::getType() const
{
        return this->type;
}

void MMFile::setType(FILE_TYPE t)
{
    type = t;
}

const MMFILE_CHAR_TYPE* MMFile::getName() const
{
    return name->data();
}

void MMFile::setName(const MMFILE_CHAR_TYPE *n)
{
    *(name) = n;
}

const MMFILE_CHAR_TYPE * MMFile::getPath() const
{
    return path->data();
}

void MMFile::setPath(const MMFILE_CHAR_TYPE *p)
{
    *(path) = p;
}


const MMFILE_CHAR_TYPE *MMFile::getSuffix() const
{
    return suffix->data();
}

void MMFile::cleanup()
{
	if (this->name)
	{
		delete this->name;
		this->name = nullptr;
	}
	if (this->path)
	{
		delete this->path;
		this->path = nullptr;
	}
	if (this->suffix)
	{
		delete this->suffix;
		this->suffix = nullptr;
	}
	if (this->absolutePath)
	{
		delete this->absolutePath;
		this->absolutePath = nullptr;
	}
		
}

void MMFile::analysisSuffix()
{
    //printf("%s\t",name->c_str());
	if (this->name)
	{
		int i = 0;
		for (i = name->size() - 1; i >= 0; i--)
		{
			if (*(name->data() + i) == '.')
				break;
		}
		if (i > 0)
		{
			//printf("%d  %s\n", i,temp.c_str());
			suffix = new MMFILE_STR_TYPE(name->substr(i));
		}
		else {
			suffix = new MMFILE_STR_TYPE();
		}
	}
}


const MMFILE_CHAR_TYPE * MMFile::getAbsolutePath() const
{
    return absolutePath->c_str();
}

void MMFile::analysisAbsolutePath()
{
	if (this->name && this->path)
	{
		absolutePath = new MMFILE_STR_TYPE();
		absolutePath->operator+=(path->c_str());
		#ifdef PF_WIN32
		absolutePath->operator+=(L"\\");
		#else
		absolutePath->operator+=('/');
		#endif
		absolutePath->operator+=(name->c_str());
	}
}


bool MMFile::nameIsLike(const MMFILE_CHAR_TYPE * str) const
{
	return  name->find(str) != MMFILE_STR_TYPE::npos;
}
#ifndef PF_WIN32

int lstrcmpW(const MMFILE_CHAR_TYPE* s1, const MMFILE_CHAR_TYPE* s2)
{
	if(s1 == nullptr && s2 == nullptr ) return 0;
	if(s1 == nullptr && s2 != nullptr ) return (int)*s2;
	if(s1 != nullptr && s2 == nullptr ) return (int)*s1;
	
	auto p1 = s1,p2 = s2;
	for (;;)
	{
		if(*p1 == '\0')
		{
			if(*p2 == '\0') return 0;
			else return (int)*p2;
		}
		if(*p2 == '\0')
		{
			if(*p1 == '\0') return 0;
			else return (int)*p1;
		}
		if(*p1 != *p2) return (int)(*p1 - *p2);
		++p1;++p2;
	}
	return 0;
}
#endif// PF_WIN32