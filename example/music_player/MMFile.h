//
// Created by wws on 17-7-19.
//

#ifndef FIRST_MMFILE_H
#define FIRST_MMFILE_H

#include <iostream>
#include <string>

#ifdef PF_WIN32
#define MMFILE_STR_TYPE std::wstring
#define MMFILE_CHAR_TYPE wchar_t
#define MMFILE_FLAC_LIT_STR L".flac"
#else
#define MMFILE_STR_TYPE std::string
#define MMFILE_CHAR_TYPE char
#define MMFILE_FLAC_LIT_STR ".flac"
#endif

class MMFile {
public:
	enum FILE_TYPE
	{
		TYPE_DIR = 0,
		TYPE_FILE
	};

private:

	FILE_TYPE type;
    MMFILE_STR_TYPE *name;
    MMFILE_STR_TYPE *path;
    MMFILE_STR_TYPE *suffix;
    MMFILE_STR_TYPE *absolutePath;
public:

    MMFile();
    MMFile(FILE_TYPE type,const MMFILE_CHAR_TYPE *name,const MMFILE_CHAR_TYPE *path);
    MMFile(const MMFile &mmf);
    MMFile(MMFile &&mmf);
	MMFile& operator=(const MMFile &mmf);
	MMFile& operator=(MMFile &&mmf);

	FILE_TYPE getType() const;
    void setType(FILE_TYPE t);

	const MMFILE_CHAR_TYPE *getName() const;
    void setName(const MMFILE_CHAR_TYPE* n);

    const MMFILE_CHAR_TYPE *getPath() const;
    void setPath(const MMFILE_CHAR_TYPE *p);

	const MMFILE_CHAR_TYPE *getSuffix() const;
	const MMFILE_CHAR_TYPE *getAbsolutePath() const;
    MMFILE_STR_TYPE getNameNoSuffix() const;
    MMFILE_STR_TYPE getAbsolutePathNoSuffix() const;
	bool nameIsLike(const MMFILE_CHAR_TYPE *str) const;

    virtual  ~MMFile();

    const MMFILE_STR_TYPE& get_name() const
    {
        return *name;
    }

    const MMFILE_STR_TYPE& get_path() const
    {
        return *path;
    }

    const MMFILE_STR_TYPE& get_suffix() const
    {
        return *suffix;
    }

    const MMFILE_STR_TYPE& get_absolutePath() const
    {
        return *absolutePath;
    }


private:
	void cleanup();
    void analysisSuffix();
    void analysisAbsolutePath();
};

#ifndef PF_WIN32
int lstrcmpW(const MMFILE_CHAR_TYPE*, const MMFILE_CHAR_TYPE*);
#endif //PF_WIN32
#endif //FIRST_MMFILE_H
