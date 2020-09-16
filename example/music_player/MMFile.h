//
// Created by wws on 17-7-19.
//

#ifndef FIRST_MMFILE_H
#define FIRST_MMFILE_H

#include <iostream>
#include <string>

class MMFile {
public:
	enum FILE_TYPE
	{
		TYPE_DIR = 0,
		TYPE_FILE
	};

private:

	FILE_TYPE type;
    std::wstring *name;
    std::wstring *path;
    std::wstring *suffix;
    std::wstring *absolutePath;
public:

    MMFile();
    MMFile(FILE_TYPE type,const wchar_t *name,const wchar_t *path);
    MMFile(const MMFile &mmf);
    MMFile(MMFile &&mmf);
	MMFile& operator=(const MMFile &mmf);
	MMFile& operator=(MMFile &&mmf);

	FILE_TYPE getType() const;
    void setType(FILE_TYPE t);

	const wchar_t *getName() const;
    void setName(const wchar_t* n);

    const wchar_t *getPath() const;
    void setPath(const wchar_t *p);

	const wchar_t *getSuffix() const;
	const wchar_t *getAbsolutePath() const;

	bool nameIsLike(const wchar_t *str) const;

    virtual  ~MMFile();

    const std::wstring& get_name() const
    {
        return *name;
    }

    const std::wstring& get_path() const
    {
        return *path;
    }

    const std::wstring& get_suffix() const
    {
        return *suffix;
    }

    const std::wstring& get_absolutePath() const
    {
        return *absolutePath;
    }


private:
	void cleanup();
    void analysisSuffix();
    void analysisAbsolutePath();
};


bool lstrcmpW(const wchar_t*, const wchar_t*);

#endif //FIRST_MMFILE_H
