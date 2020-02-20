#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <token_stream.hpp>
#include <optional>
#include <sstream>
#include <memory>

namespace gld::glsl{
    
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
        virtual std::optional<std::string> handle(std::filesystem::path path,const std::vector<token::Token>& ts,int b,int e,std::function<std::string(std::filesystem::path,std::string&&)> process_f) = 0; 
    };

    struct GlslPreprocessErr : std::runtime_error{
        GlslPreprocessErr(const char* str) : runtime_error(str){}
    };

    template<char Symbol,typename ...Args>
    class PreprocessMgr{
    public:
        PreprocessMgr()
        {
            (procs.push_back(new Args()),...);
        }

        ~PreprocessMgr()
        {
            for(auto v : procs)
            {
                delete v;
            }
        }

        std::string process(std::filesystem::path path,std::string&& str) const
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
                            std::function<std::string(std::filesystem::path,std::string&&)> process_f = [this](std::filesystem::path path,std::string&& str)->std::string
                            {
                                return this->process(std::move(path),std::move(str));
                            };
                            auto res = proc->handle(path,ts,i,e,process_f);
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

        std::optional<std::string> handle(std::filesystem::path path,const std::vector<token::Token>& ts,int b,int e,std::function<std::string(std::filesystem::path,std::string&&)> process_f) override;
    };
}