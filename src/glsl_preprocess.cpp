#include <glsl_preprocess.hpp>

#include <resource_mgr.hpp>
#ifndef PF_ANDROID
namespace fs = std::filesystem;
#else
#include <log.hpp>
#include <fileop.hpp>
using namespace dbg::literal;
#endif

namespace gld::glsl{

    std::shared_ptr<IncludeCatche> IncludeCatche::self = std::shared_ptr<IncludeCatche>();
    std::shared_ptr<IncludeCatche> IncludeCatche::get_instance()
    {
        if(!self)
        {
            self = std::make_shared<IncludeCatche>();
        }
        return self;
    }

#ifndef PF_ANDROID
    std::optional<std::string> IncludePreprocess::handle(PathTy path,const std::vector<token::Token>& ts,int b,int e,std::function<std::string(PathTy,std::string&&)> process_f)
    {
        std::string ps = path.string();
        auto in_path = path.parent_path();
        in_path /= ts[b + 1].body.c_str();
        fs::path in_absolute = fs::absolute(in_path);
        std::string in_ps = in_absolute.string();
        if(IncludeCatche::get_instance()->has(in_ps))
        {
            return std::optional<std::string>( IncludeCatche::get_instance()->map[in_ps] );
        }else{
            if(fs::exists(in_path))
            {
                auto [success,res] = LoadText::load(in_path);
                if(success)
                {
                    std::string process_res = process_f(in_path,std::move(*res));
                    IncludeCatche::get_instance()->set(in_ps,process_res);
                    return std::optional<std::string>( std::move(process_res) );
                }else{
                    std::string str("Include file load failed at ");
                    str += ps;
                    throw GlslPreprocessErr(str.c_str());
                }
            }else
            {
                std::string str("Not found include file ");
                str += ts[b + 1].body.c_str();
                str += " at ";
                str += ps;
                throw GlslPreprocessErr(str.c_str());
            }
        }
    }
#else
    

    bool is_exists(std::string& path)
    {
        return true;
    }

    std::optional<std::string> IncludePreprocess::handle(AndroidCxtPtrTy cxt,PathTy path,const std::vector<token::Token>& ts,int b,int e,std::function<std::string(PathTy,std::string&&)> process_f)
    {
        auto in_path = wws::get_parent(path);
        in_path += "/";
        in_path += ts[b + 1].body.c_str();
        auto in_ps = wws::to_absolute(in_path);

        dbg::log << "glsl perprocess @V@"_E;
        dbg::log << "to_absolute = " << in_path << " "<< in_ps << dbg::endl;

        if(IncludeCatche::get_instance()->has(in_ps))
        {
            return std::optional<std::string>( IncludeCatche::get_instance()->map[in_ps] );
        }else{
            if(is_exists(in_path))
            {
                auto [success,res] = LoadText::load(cxt,in_ps);
                if(success)
                {
                    std::string process_res = process_f(in_path,std::move(*res));
                    IncludeCatche::get_instance()->set(in_ps,process_res);
                    return std::optional<std::string>( std::move(process_res) );
                }else{
                    std::string str("Include file load failed at ");
                    str += path;
                    throw GlslPreprocessErr(str.c_str());
                }
            }else
            {
                std::string str("Not found include file ");
                str += ts[b + 1].body.c_str();
                str += " at ";
                str += path;
                throw GlslPreprocessErr(str.c_str());
            }
        }
    }
#endif

}