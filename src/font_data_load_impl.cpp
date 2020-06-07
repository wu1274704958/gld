#include <data_mgr.hpp>
#include <ft2pp.hpp>
#include <sundry.hpp>
#include <resource_mgr.hpp>

std::string gld::LoadFontLibrary::key_from_args(gld::LoadFontLibrary::ArgsTy args)
{
    return "FontLibrary";
}

gld::LoadFontLibrary::RealRetTy gld::LoadFontLibrary::load(gld::LoadFontLibrary::ArgsTy args)
{
    auto res = std::make_shared<ft2::Library>();
    return std::make_tuple(true, res);
}

gld::LoadFontLibrary::ArgsTy gld::LoadFontLibrary::default_args()
{
    return std::tuple<>();
}

std::string gld::LoadFontFace::key_from_args(gld::LoadFontFace::ArgsTy args)
{
    return sundry::format_tup(args, '#');
}
std::string gld::LoadFontFace::key_from_args(std::tuple<const char*, int,int> args)
{
    return sundry::format_tup(args, '#');
}
gld::LoadFontFace::RealRetTy   gld::LoadFontFace::load(gld::LoadFontFace::ArgsTy args)
{
    auto lib = DefDataMgr::instance()->load<DataType::FontLibrary>();
    auto font = DefResMgr::instance()->load<ResType::font>(std::forward<decltype(std::get<0>(args))>(std::get<0>(args)), std::get<1>(args));
    
    bool f = false;
    std::shared_ptr<ft2::Face> res; 
    if (font && lib )
    {
        res = std::shared_ptr<ft2::Face>(new ft2::Face( lib->load_face_for_mem<ft2::Face>(font->get_data(), font->get_size(), std::get<2>(args))));
        f = true;
    }
    return make_result(f, std::move(res));
}
gld::LoadFontFace::RealRetTy   gld::LoadFontFace::load(std::tuple<const char*, int,int> args)
{
   return  load(std::make_tuple(std::string(std::get<0>(args)), std::get<1>(args), std::get<1>(args)));
}
