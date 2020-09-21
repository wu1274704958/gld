#include "lrc_analyse.h"


namespace lrc{

    size_t LrcAnalyse::analyse(std::shared_ptr<LrcType> lrc,double s,size_t last)
    {
        if(last <= -1)
            last = 0;
        auto& ls = lrc->data;
        auto len = ls.size();
        for(int i = last;i < len;++i)
        {
            if(ls[i].second >= s)
                return i - 1;
        }
        return -1;
    }
    
}