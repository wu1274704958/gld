#pragma once

#ifdef PF_ANDROID
#include <assimp/IOSystem.hpp>
#include <assimp/IOStream.hpp>
#include <EGLCxt.h>
#include <memory>

struct AAsset;

namespace gld
{

class AAndIosSys : public Assimp::IOSystem
{
    
public:
    AAndIosSys(std::shared_ptr<EGLCxt> cxt):cxt(std::move(cxt)) {}
    bool Exists( const char* pFile) const override;
    char getOsSeparator() const override;
    Assimp::IOStream* Open(const char* pFile,const char* pMode = "rb") override;
    void Close( Assimp::IOStream* pFile) override;

protected:
    std::shared_ptr<EGLCxt> cxt;
};

class AAndIosStream : public Assimp::IOStream
{
    public:
    AAndIosStream(AAsset* asset);
    ~AAndIosStream();
    size_t Read(void* pvBuffer,size_t pSize,size_t pCount) override;
    size_t Write(const void* pvBuffer,size_t pSize,size_t pCount) override;
    aiReturn Seek(size_t pOffset,aiOrigin pOrigin) override;
    size_t Tell() const override;
    size_t FileSize() const override;
    void Flush() override;

protected:
    AAsset* asset;
    size_t pos;
};
    
} // namespace gld
#endif



