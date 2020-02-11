#include <glsl_preprocess.hpp>

#include <resource_mgr.hpp>

namespace fs = std::filesystem;

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

    std::optional<std::string> IncludePreprocess::handle(std::filesystem::path path,const std::vector<token::Token>& ts,int b,int e,std::function<std::string(std::filesystem::path,std::string&&)> process_f)
    {
        std::string ps = path.string();
        std::filesystem::path in_path = path.parent_path();
        in_path /= ts[b + 1].body.c_str();
        fs::path in_absolute = fs::absolute(in_path);
        std::string in_ps = in_absolute.string();
        if(IncludeCatche::get_instance()->has(in_ps))
        {
            return std::optional<std::string>( IncludeCatche::get_instance()->map[in_ps] );
        }else{
            if(fs::exists(in_path))
            {
                auto res = LoadText::load(in_path);
                if(res)
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


}