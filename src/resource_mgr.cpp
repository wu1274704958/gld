#include "resource_mgr.hpp"
#include <filesystem>
#include <string>
#include <fstream>
#include <comm.hpp>
#include <glad/glad.h>
#define STB_IMAGE_IMPLEMENTATION
#include <3rd_party/stb/stb_image.h>
#include <glsl_preprocess.hpp>
using namespace std;

unsigned int gld::StbImage::gl_format()
{
	GLenum format;
	switch (channel)
	{
		case 1:
			format = GL_RED;
			break;
		case 3:
			format = GL_RGB;
			break;
		case 4:
			format = GL_RGBA;
			break;
	} 
	return static_cast<unsigned int>(format);
}

gld::StbImage::~StbImage()
{
	if (data)
		stbi_image_free(data);
}


#ifndef PF_ANDROID
namespace fs = std::filesystem;


std::unique_ptr<std::string> gld::LoadText::load(fs::path p)
{
	std::string path = p.string();
	std::ifstream f(path.c_str(), std::ios::binary);
	
	if (f.good())
	{
		string *res = new string();
		res->reserve(1024);
		while (!f.eof())
		{
			char buf[256] = { 0 };
			f.read(buf, wws::arrLen(buf) - 1);
			res->append(buf);
		}
		return std::unique_ptr<std::string>(res);
	}
	else
		return std::unique_ptr<std::string>();
}

std::unique_ptr<std::string> gld::LoadTextWithGlslPreprocess::load(fs::path p)
{
	auto ptr = LoadText::load(p);
	
	if (ptr)
	{
		glsl::PreprocessMgr<'#',glsl::IncludePreprocess> preprocess;

		std::string res = preprocess.process(std::move(p),std::move(*ptr));
		return std::unique_ptr<std::string>(new std::string(std::move(res)));
	}
	else
		return std::unique_ptr<std::string>();
}

#ifdef LoadImage
#undef LoadImage
#endif
std::unique_ptr<gld::StbImage> gld::LoadImage::load(std::filesystem::path p,int req_comp)
{
	std::string path = p.string();
	int width, height, nrComponents;
	unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrComponents, req_comp);
	if (data)
	{
		auto res = new gld::StbImage();
		res->data = data;
		res->width = width;
		res->height = height;
		res->channel = nrComponents;
		return std::unique_ptr<gld::StbImage>(res);
	}
	else
		return std::unique_ptr<gld::StbImage>();
}

#else

#endif