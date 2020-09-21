#pragma once
#include "lrc_loader.h"
#include "../MMFile.h"
#include <comm.hpp>
#include "lrc_analyse.h"


namespace lrc{

template <class T,class Args>											
using has_load_lrc_func_t = decltype(std::declval<T>().load(std::declval<Args>()));

template <typename T,typename Args>
using has_load_lrc_func_vt = wws::is_detected<has_load_lrc_func_t,T,Args>;

template <class T,class ...Args>											
using has_analyse_lrc_func_t = decltype(std::declval<T>().analyse(std::declval<Args>() ...));

template <typename T,typename ... Args>
using has_analyse_lrc_func_vt = wws::is_detected<has_analyse_lrc_func_t,T,Args...>;

template <typename LOAD,typename Analyse>
struct LrcMgr
{
    using LRC_TYPE  = typename LOAD::LrcType;
    static_assert(has_load_lrc_func_vt<LOAD,std::string>::value,"Bad lrc loader type!!!");
    static_assert(has_analyse_lrc_func_vt<Analyse,std::shared_ptr<LRC_TYPE>,double,size_t>::value,"Bad lrc analyse type!!!");
    void onplay(const MMFile& f)
    {
        auto m = dbg(f.getAbsolutePathNoSuffix());
        m += ".lrc";
        curr = load.load(m);
        last_idx = -1;
    }
    void playing(double s)
    {
        size_t idx = -1;
        if(curr)
        {
            idx = analyse.analyse(curr,s,last_idx);
        }
        if(idx >= 0 && idx != last_idx)
        {
            printf("%lf : %s\n",curr->data[idx].second, curr->data[idx].line.c_str() );
        }
            
        last_idx = idx;
    }
    LOAD load;
    Analyse analyse;
    std::shared_ptr<typename LOAD::LrcType> curr;
    size_t last_idx = -1;
};

typedef LrcMgr<LrcLoader,LrcAnalyse> DefLrcMgr;

}