#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <token_stream.hpp>
#include <optional>
#include <sstream>

namespace gld::glsl{
    
    class IncludeCatche{

    };

    class Preprocess{
        public:
        virtual ~Preprocess(){}
        virtual bool interest(const std::vector<token::Token>& ts,int b,int e) = 0;
        virtual std::optional<std::string> handle(std::filesystem::path path,const std::vector<token::Token>& ts,int b,int e) = 0;

    };

    struct GlslPreprocessErr : std::runtime_error{
        GlslPreprocessErr(const char* str) : runtime_error(str){}
    };

    template<char Symbol>
    class PreprocessMgr{
    public:
        PreprocessMgr(std::initializer_list<Preprocess> list)
        {
            for(auto &v : list)
            {
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
                            throw std::GlslPreprocessErr("Syntax error!");
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
                                ts.insert(ts.begin() + i,token::Token(std::move(res.value()),token::Token::None,'\n');
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
        std::vector<Preprocess> procs;    
    };
}