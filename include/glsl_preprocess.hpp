#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <token_stream.hpp>
#include <optional>
#include <sstream>
#include <memory>

#ifdef PF_ANDROID
#include <EGLCxt.h>
#endif

namespace gld::glsl{
#ifndef PF_ANDROID
    using PathTy = std::filesystem::path;
#else
    using PathTy = std::string;
    using AndroidCxtPtrTy = std::shared_ptr<EGLCxt>;
#endif

    struct IncludeCatche{

        IncludeCatche(){}

        std::unordered_map<std::string,std::string> map;

        bool has(std::string& path) const 
        {
            return map.find(path) != map.end();
        }

        void set(std::string& path,std::string& str)
        {
            map[path] = str;
        }

        static std::shared_ptr<IncludeCatche> self;
        static std::shared_ptr<IncludeCatche> get_instance();
    };

    class Preprocess{
        public:

        virtual ~Preprocess(){}
        virtual bool interest(const std::vector<token::Token>& ts,int b,int e) = 0;
#ifndef PF_ANDROID        
        virtual std::optional<std::string> handle(PathTy path,const std::vector<token::Token>& ts,int b,int e,std::function<std::string(PathTy,std::string&&)> process_f) = 0;
#else
        virtual std::optional<std::string> handle(AndroidCxtPtrTy,PathTy path,const std::vector<token::Token>& ts,int b,int e,std::function<std::string(PathTy,std::string&&)> process_f) = 0; 
#endif
    };

    struct GlslPreprocessErr : std::runtime_error{
        GlslPreprocessErr(const char* str) : runtime_error(str){}
    };

    template<char Symbol,typename ...Args>
    class PreprocessMgr{
    public:
#ifndef PF_ANDROID
        PreprocessMgr()
        {
            (procs.push_back(new Args()),...);
        }
#else
        PreprocessMgr(AndroidCxtPtrTy cxt) : cxt(std::move(cxt))
        {
            (procs.push_back(new Args()),...);
        }
#endif
        ~PreprocessMgr()
        {
            for(auto v : procs)
            {
                delete v;
            }
        }

        std::string process(PathTy path,std::string&& str) const
        {
            token::TokenStream<std::string> tos(std::move(str));
            tos.analyse();
            auto& ts = tos.tokens;
            for(int i = 0;i < ts.size();++i)
            {
                bool is_new_line = i == 0 ? true : ts[i - 1].back == '\n';
                if(is_new_line && ts[i].per == Symbol)
                {
                    int e = i;
                    while(ts[e].back != '\n')
                    { 
                        if(e + 1 >= ts.size())
                            goto OutLoop;
                        ++e; 
                    }
                    for(auto& proc : procs)
                    {
                        if(proc->interest(ts,i,e))
                        {
                            std::function<std::string(PathTy,std::string&&)> process_f = [this](PathTy path,std::string&& str)->std::string
                            {
                                return this->process(std::move(path),std::move(str));
                            };
#ifndef PF_ANDROID        
                            auto res = proc->handle(path,ts,i,e,process_f);
#else
                            auto res = proc->handle(cxt,path,ts,i,e,process_f);
#endif
                            if(res)
                            {
                                ts.erase(ts.begin() + i,ts.begin() + (e + 1));
                                ts.insert(ts.begin() + i,token::Token(std::move(*res),token::Token::None,'\n'));
                                break;
                            }
                        }
                    }
                }
            }
            OutLoop:
            std::ostringstream os;
            tos.save(os);
            return os.str();
        }
    protected:
        std::vector<Preprocess*> procs;  
#ifdef PF_ANDROID   
        AndroidCxtPtrTy cxt;
#endif
    };

    class IncludePreprocess : public Preprocess
    {
        bool interest(const std::vector<token::Token>& ts,int b,int e) override
        {
            if(b == e)
                return false;
            return ts[b].body == "include" && ts[b + 1].per == '"' && ts[b + 1].back == '"' && !is_empty(ts[b + 1].body);
        }

        bool is_empty(const std::string& str)
        {
            return str.empty() && str[0] == ' ';
        }
#ifndef PF_ANDROID        
        std::optional<std::string> handle(PathTy path,const std::vector<token::Token>& ts,int b,int e,std::function<std::string(PathTy,std::string&&)> process_f) override;
#else
        std::optional<std::string> handle(AndroidCxtPtrTy,PathTy path,const std::vector<token::Token>& ts,int b,int e,std::function<std::string(PathTy,std::string&&)> process_f) override;
#endif
    };
}