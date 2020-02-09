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

    template<typename Par>
    class Preprocess{
        public:

        virtual ~Preprocess(){}
        virtual bool interest(const std::vector<token::Token>& ts,int b,int e) = 0;
        virtual std::optional<std::string> handle(std::filesystem::path path,const std::vector<token::Token>& ts,int b,int e) = 0; 
        void set_parent(Par* p){
            parent = p;
        }
        protected:
        Par* parent = null;
    };

    struct GlslPreprocessErr : std::runtime_error{
        GlslPreprocessErr(const char* str) : runtime_error(str){}
    };

    template<char Symbol>
    class PreprocessMgr{
    public:
        PreprocessMgr(std::initializer_list<Preprocess<PreprocessMgr<Symbol>>> list)
        {
            for(auto &v : list)
            {
                v.set_parent(this);
                procs.push_back(std::move(v));
            }
        }

        std::string process(std::filesystem::path path,std::string&& str)
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
                            throw GlslPreprocessErr("Syntax error!");
                        ++e; 
                    }
                    for(auto& proc : procs)
                    {
                        if(proc.interest(ts,i,e))
                        {
                            auto res = proc.handle(std::move(path),ts,i,e);
                            if(res)
                            {
                                ts.erase(ts.begin() + i,ts.begin() + (e + 1));
                                ts.insert(ts.begin() + i,token::Token(std::move(res.value()),token::Token::None,'\n'));
                                break;
                            }
                        }
                    }
                }
            }
            std::ostringstream os;
            tos.save(os);
            return os.str();
        }
    protected:
        std::vector<Preprocess<PreprocessMgr<Symbol>>> procs;    
    };

    template<typename T>
    class IncludePreprocess : public Preprocess<T>
    {
        bool interest(const std::vector<token::Token>& ts,int b,int e) override
        {
            if(b == e)
                return false;
            return ts[b].body == "include" && ts[b + 1].per == '"' && ts[b + 1].back == '"' && !is_empty(ts[b + 1].body);
        }

        bool is_empty(std::string& str)
        {
            return str.empty() && str[0] == ' ';
        }

        virtual std::optional<std::string> handle(std::filesystem::path path,const std::vector<token::Token>& ts,int b,int e) override
        {
            return std::nullopt_t;
        }
    };
}