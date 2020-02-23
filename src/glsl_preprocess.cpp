#include <glsl_preprocess.hpp>

#include <resource_mgr.hpp>
#ifndef PF_ANDROID
namespace fs = std::filesystem;
#include <vector>
#include <stack>
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
#else
    std::string get_parent(const std::string& path)
    {
        if(path.empty()) return path;
        for(int i = path.size() - 1;i >= 0; --i)
        {
            if(path[i] == '/')
            {
                return path.substr(0,i);
            }
        }
        return path;
    }

    bool is_absolute(const std::string& path)
    {
        
    }

    std::string to_absolute(const std::string& path)
    {
        if(path.empty()) return path;
        int b = 0;
        std::stack<std::string> sta;
        for(int i = 0;i < path.size();++i)
        {
            if(path[i] == '/' && b < i)
            {
                auto str = path.substr(b,i - b);
                if(str == ".")
                {}else 
                if(str == "..")
                {
                    if(sta.empty())
                        throw std::runtime_error("Bad path for func to_absolute!!");
                    sta.pop();
                }else{
                    sta.push(str);
                }
                b = i + 1;++i;
            }
        }
        std::string res;
        while (!sta.empty())
        {
            res.insert(0,sta.top().c_str());
            sta.pop();
        }
        return res;
    }

    bool is_exists(std::string& path)
    {
        return true;
    }

    std::optional<std::string> IncludePreprocess::handle(AndroidCxtPtrTy cxt,PathTy path,const std::vector<token::Token>& ts,int b,int e,std::function<std::string(PathTy,std::string&&)> process_f)
    {
        auto in_path = get_parent(path);
        in_path += ts[b + 1].body.c_str();
        auto in_ps = to_absolute(in_path);

        if(IncludeCatche::get_instance()->has(in_ps))
        {
            return std::optional<std::string>( IncludeCatche::get_instance()->map[in_ps] );
        }else{
            if(is_exists(in_path))
            {
                auto res = LoadText::load(cxt,in_path);
                if(res)
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