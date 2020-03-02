#include "resource_mgr.hpp"
#include <filesystem>
#include <string>
#include <fstream>
#include <comm.hpp>
#include <glad/glad.h>
#define STB_IMAGE_IMPLEMENTATION
#include <3rd_party/stb/stb_image.h>
#include <glsl_preprocess.hpp>
#include <serialization.hpp>

#include <assimp/scene.h>
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

std::string gld::LoadScene::format_args(unsigned int flag)
{
    return wws::to_string(flag);
}

gld::LoadScene::ArgsTy gld::LoadScene::default_args()
{
	return aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace;
}

#ifndef PF_ANDROID
namespace fs = std::filesystem;


gld::LoadText::RealRetTy gld::LoadText::load(fs::path p)
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
		return std::make_tuple(true,std::shared_ptr<std::string>(res));
	}
	else
		return std::make_tuple(false,std::shared_ptr<std::string>());
}

gld::LoadTextWithGlslPreprocess::RealRetTy gld::LoadTextWithGlslPreprocess::load(fs::path p)
{
	auto ptr = DefResMgr::instance()->load<ResType::text>(p.string());
	
	if (ptr)
	{
		glsl::PreprocessMgr<'#',glsl::IncludePreprocess> preprocess;

		std::string res = preprocess.process(std::move(p),std::string(*ptr));
		return std::make_tuple(true,std::shared_ptr<std::string>(new std::string(std::move(res))));
	}
	else
		return std::make_tuple(false,std::shared_ptr<std::string>());
}

#ifdef LoadImage
#undef LoadImage
#endif
gld::LoadImage::RealRetTy gld::LoadImage::load(std::filesystem::path p,int req_comp)
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
		return std::make_tuple(true,std::shared_ptr<gld::StbImage>(res));
	}
	else
		return std::make_tuple(false,std::shared_ptr<gld::StbImage>());
}


gld::LoadScene::RealRetTy gld::LoadScene::load(gld::PathTy p,gld::LoadScene::ArgsTy flag)
{
	std::string path = p.string();
	Assimp::Importer* importer = new Assimp::Importer();
    const aiScene* scene = importer->ReadFile(path, flag);
	if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) // if is Not Zero
    {
        delete importer;
		return std::make_tuple(false,std::shared_ptr<Assimp::Importer>());
    }else{
		return std::make_tuple(true,std::shared_ptr<Assimp::Importer>(importer));
	}
}

#else

#include <EGLCxt.h>
#include <android/asset_manager.h>
#include <native_app_glue/android_native_app_glue.h>
#include <log.hpp>
#include <assimp_iosystem.h>
using  namespace dbg::literal;

std::tuple<std::unique_ptr<char[]>,std::uint64_t> load_byte(gld::AndroidCxtPtrTy cxt,const std::string& uri)
{
	AAssetManager* mgr = cxt->app->activity->assetManager;

	AAsset* asset = AAssetManager_open(mgr,uri.c_str(),AASSET_MODE_BUFFER);
	if (asset)
	{
		off_t len = AAsset_getLength(asset);
		if(len > 0) {
			char *res = new char[len];
			std::memcpy(res, AAsset_getBuffer(asset), static_cast<size_t>(len));
			AAsset_close(asset);
			return std::make_tuple(std::unique_ptr<char[]>(res), static_cast<std::uint64_t>(len));
		}
	}
	return std::make_tuple(std::unique_ptr<char[]>(),0L);
}

gld::LoadText::RealRetTy gld::LoadText::load(gld::AndroidCxtPtrTy cxt,std::string path)
{

	auto [ptr,len] = load_byte(cxt,path);
	if (ptr)
	{
		std::string *res = new string();
		res->resize(len);
		std::memcpy(res->data(), ptr.get(), static_cast<size_t>(len));
		return std::make_tuple(true,std::shared_ptr<std::string>(res));
	}
	else
		return std::make_tuple(false,std::shared_ptr<std::string>());
}

gld::LoadImage::RealRetTy gld::LoadImage::load(gld::AndroidCxtPtrTy cxt,std::string path,int req_comp)
{

	int width, height, nrComponents;
	//dbg::log << "res mgr @V@"_E;
	AAssetManager* mgr = cxt->app->activity->assetManager;
	//dbg::log << (mgr == nullptr) << dbg::endl;
	AAsset* asset = AAssetManager_open(mgr,path.c_str(),AASSET_MODE_BUFFER);

	if (asset)
	{
		off_t len = AAsset_getLength(asset);
		unsigned char* data = stbi_load_from_memory(static_cast<const stbi_uc *>(AAsset_getBuffer(asset)),
							  static_cast<int>(len), &width, &height, &nrComponents, req_comp);

		auto res = new gld::StbImage();
		res->data = data;
		res->width = width;
		res->height = height;
		res->channel = nrComponents;
		return std::make_tuple(true,std::shared_ptr<gld::StbImage>(res));
	}
	else
		return std::make_tuple(false,std::shared_ptr<gld::StbImage>());
}


gld::LoadTextWithGlslPreprocess::RealRetTy gld::LoadTextWithGlslPreprocess::load(gld::AndroidCxtPtrTy cxt,std::string path)
{
	auto ptr = DefResMgr::instance()->load<ResType::text>(std::string(path));
	dbg::log << "res mgr @V@"_E << "LoadTextWithGlslPreprocess " << path.c_str() << dbg::endl;
	if (ptr)
	{
		glsl::PreprocessMgr<'#',glsl::IncludePreprocess> preprocess(cxt);

		std::string res = preprocess.process(std::move(path),std::string(*ptr));
		return std::make_tuple(true,std::shared_ptr<std::string>(new std::string(std::move(res))));
	}
	else
		return std::make_tuple(false,std::shared_ptr<std::string>());
}

gld::LoadScene::RealRetTy gld::LoadScene::load(gld::AndroidCxtPtrTy cxt,gld::PathTy p,gld::LoadScene::ArgsTy flag)
{
	auto [ptr,len] = load_byte(cxt,p);
	dbg::log << "res mgr @V@"_E << "LoadSceneWithGlslPreprocess " << (bool)ptr << " len = " << len << dbg::endl;
	if(ptr)
	{
		Assimp::Importer* importer = new Assimp::Importer();
		importer->SetIOHandler(new AAndIosSys(cxt));
		const aiScene* scene = importer->ReadFile(p,flag);

		dbg::log << "(scene == nullptr) " << (scene == nullptr) << dbg::endl;
		dbg::log << "msg " << importer->GetErrorString() << dbg::endl;
		if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) // if is Not Zero
		{
			delete importer;
			return std::make_tuple(false,std::shared_ptr<Assimp::Importer>());
		}else{
			return std::make_tuple(true,std::shared_ptr<Assimp::Importer>(importer));
		}
	}
	return std::make_tuple(false,std::shared_ptr<Assimp::Importer>());
}



bool gld::AAndIosSys::Exists( const char* pFile) const 
{
	auto mgr = cxt->app->activity->assetManager;
	auto assets = AAssetManager_open(mgr,pFile,AASSET_MODE_BUFFER);
	bool res = (assets != nullptr);
	AAsset_close(assets);
	return res;
}

char gld::AAndIosSys::getOsSeparator() const 
{
	return '/';
}

Assimp::IOStream* gld::AAndIosSys::Open(const char* pFile,const char* pMode) 
{
	AAssetManager* mgr = cxt->app->activity->assetManager;

	AAsset* asset = AAssetManager_open(mgr,pFile,AASSET_MODE_STREAMING);
	if (asset)
	{
		return new AAndIosStream(asset);
	}
	return nullptr;
}

void gld::AAndIosSys::Close( Assimp::IOStream* pFile) 
{
	delete pFile;
}

gld::AAndIosStream::AAndIosStream(AAsset* asset)
{
	this->asset = asset;
	pos = 0;
}
gld::AAndIosStream::~AAndIosStream()
{
	if(asset)
		AAsset_close(asset);
}
size_t gld::AAndIosStream::Read(void* pvBuffer,size_t pSize,size_t pCount)
{
	size_t p = static_cast<size_t>(AAsset_read(asset, pvBuffer, pSize * pCount));
	if(p > 0)
		pos += p;
	return p;
}
size_t gld::AAndIosStream::Write(const void* pvBuffer,size_t pSize,size_t pCount)
{
	return 0;
}
aiReturn gld::AAndIosStream::Seek(size_t pOffset,aiOrigin pOrigin)
{
	off64_t p = -1;
	if((p = AAsset_seek64(asset,pOffset,pOrigin)) == -1)
	{
		return AI_FAILURE;
	}else
	{
		pos = static_cast<size_t>(p);
		return AI_SUCCESS;
	}
}
size_t gld::AAndIosStream::Tell() const
{
	return pos;
}

size_t gld::AAndIosStream::FileSize() const
{
	return static_cast<size_t>(AAsset_getLength(asset));
}

void gld::AAndIosStream::Flush()
{

}


#endif