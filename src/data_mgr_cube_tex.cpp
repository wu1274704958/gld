
#include <data_mgr.hpp>
#include <resource_mgr.hpp>
#include <log.hpp>
#include <sundry.hpp>
#include <program.hpp>
#include <serialization.hpp>
#include <component.h>
#include <comps/Material.hpp>
#include <assimp/scene.h>
#include <fileop.hpp>
#include <cube_texture.hpp>

using namespace dbg::literal;

std::string gld::LoadTextureCube::key_from_args(gld::LoadTextureCube::ArgsTy args)
{
    std::string res = std::move(std::get<0>(args));
    res += "#TextureCube#";
    res += std::get<1>(args);
    res += '#';
    res += wws::to_string(std::get<2>(args));
    return res;
}
std::string gld::LoadTextureCube::key_from_args(std::tuple<const char*,const char*,int> args)
{
    std::string res = std::get<0>(args);
    res += "#TextureCube#";
    res += std::get<1>(args);
    res += '#';
    res += wws::to_string(std::get<2>(args));
    return res;
}

std::shared_ptr<gld::StbImage> load_image(std::string path,int flag)
{
    auto image = gld::ResMgrWithGlslPreProcess::instance()->load<gld::ResType::image>(std::move(path),flag);
    return image;
}

gld::LoadTextureCube::RealRetTy gld::LoadTextureCube::load(gld::LoadTextureCube::ArgsTy args)
{
    dbg::log << "data mgr @V@"_E ;
    auto [path,suff,flag] = args;
    std::shared_ptr<CubeTexture> res;
    bool f = false;

    auto top = load_image(path+"/top."+suff,flag);
    auto bottom = load_image(path+"/bottom."+suff,flag);
    auto left = load_image(path+"/left."+suff,flag);
    auto right = load_image(path+"/right."+suff,flag);
    auto front = load_image(path+"/front."+suff,flag);
    auto back = load_image(path+"/back."+suff,flag);

    if(top && bottom && left && right && front && back)
    {
        f = true;
        res = std::shared_ptr<CubeTexture>(new CubeTexture());
        res->create();
        res->bind();
        res->tex_image_cube({
            TexData2DWarp<unsigned char>{ 0,static_cast<int>(top->gl_format()),0,top->gl_format(),top->data,top->width,top->height },
            TexData2DWarp<unsigned char>{ 0,static_cast<int>(bottom->gl_format()),0,bottom->gl_format(),bottom->data,bottom->width,bottom->height },
            TexData2DWarp<unsigned char>{ 0,static_cast<int>(left->gl_format()),0,left->gl_format(),left->data,left->width,left->height },
            TexData2DWarp<unsigned char>{ 0,static_cast<int>(right->gl_format()),0,right->gl_format(),right->data,right->width,right->height },
            TexData2DWarp<unsigned char>{ 0,static_cast<int>(front->gl_format()),0,front->gl_format(),front->data,front->width,front->height },
            TexData2DWarp<unsigned char>{ 0,static_cast<int>(back->gl_format()),0,back->gl_format(),back->data,back->width,back->height }
        });
        res->unbind();
        return make_result(f,std::move(res));
    }
    return make_result(f,std::move(res));
}
gld::LoadTextureCube::RealRetTy   gld::LoadTextureCube::load(std::tuple<const char*,const char*,int> args)
{
    return load(std::make_tuple(std::string(std::get<0>(args)),std::string(std::get<1>(args)),std::get<2>(args)));
}