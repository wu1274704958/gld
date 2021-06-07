#include  "lrc_loader.h"
#include <filesystem>
#include <fileop.hpp>
#include <serialization.hpp>
#include <json.hpp>
namespace fs = std::filesystem;

namespace lrc{

    double parse_time(const std::string& s)
    {
        int b = 0;
        double res = 0;
        for (int i = 0; i < s.size(); ++i)
        {
            if (s[i] == ':')
            {
                auto ms = s.substr(b, i - b);
                int m = wws::parser<int>(ms);
                res += static_cast<double>(m) * 60.0;
                b = i + 1;
                break;
            }
        }
        if (b < s.size())
        {
            auto ms = s.substr(b, s.size() - b);
            res += wws::parser<double>(ms);
        }
        return res;
    }

    std::shared_ptr<LrcLoader::LrcType> load_inside(std::optional<std::string> res)
    {
        if (res && !res.value().empty())
        {
            auto lrc = std::shared_ptr<Lrc>(new Lrc());
            auto cnt = res.value();
            int tb = 0, cb = -1;
            double ti = 0.0;
            bool parse_time_err = false;
            for (int i = 0; i < cnt.size(); ++i)
            {
                if (cnt[i] == '[') tb = i + 1;
                if (cnt[i] == ']')
                {
                    auto time = cnt.substr(tb, i - tb);
                    try {
                        ti = parse_time(time);
                        cb = i + 1;
                    }
                    catch (...) {
                        parse_time_err = true;
                    }
                }
                if (parse_time_err && cnt[i] == '\n')
                {
                    parse_time_err = false;
                    continue;
                }
                if (cnt[i] == '\n' && cb > -1)
                {
                    auto str = cnt.substr(cb, i - cb);
                    lrc->data.push_back(Line(ti, str));
                    cb = -1;
                }
            }
            return lrc;
        }
        else {
            return nullptr;
        }
    }

    std::shared_ptr<LrcLoader::LrcType> LrcLoader::load(const std::string& path)
    {
        if(map.find(path) != map.end())
            return map[path];
        fs::path pa(path);
        if(fs::exists(pa))
        {
            auto str = wws::read_from_file<1024>(pa,std::ios::binary);
            if (str && str->at(0) == '{' && str->back() == '}')
            {
                try {
                    wws::Json json(str.value());
                    wws::Json& lrc = json.get_obj("lrc");
                    auto& str = lrc.get_str("lyric");
                    for (int64_t i = 0; i < str.size(); ++i)
                    {
                        if (str[i] == '\\' && i + 1 < str.size() && str[i + 1] == 'n')
                        {
                            str[i] = '\n';
                            str.erase(i + 1, 1);
                        }
                    }
                    auto res = load_inside(std::optional(std::move(str)));
                    if (res)
                    {
                        map[path] = res;
                        return res;
                    }
                    return nullptr;
                }
                catch (std::exception e) { return nullptr; }
            }
            else
            {
                auto res = load_inside(std::move(str));
                if (res)
                {
                    map[path] = res;
                    return res;
                }
            }
        }else{
            return nullptr;
        }
    }

    
}