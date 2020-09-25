#pragma once
#include "lrc_loader.h"
#include "../MMFile.h"
#include <comm.hpp>
#include "lrc_analyse.h"
#include "lrc_view.h"
#ifdef PF_WIN32
#include <tools/convert.h>
#endif // PF_WIN32



namespace lrc{

template <class T,class Args>											
using has_load_lrc_func_t = decltype(std::declval<T>().load(std::declval<Args>()));

template <typename T,typename Args>
using has_load_lrc_func_vt = wws::is_detected<has_load_lrc_func_t,T,Args>;

template <class T,class ...Args>											
using has_analyse_lrc_func_t = decltype(std::declval<T>().analyse(std::declval<Args>() ...));

template <typename T,typename ... Args>
using has_analyse_lrc_func_vt = wws::is_detected<has_analyse_lrc_func_t,T,Args...>;

template <class T,class ...Args>											
using has_onplay_lrc_func_t = decltype(std::declval<T>().on_play(std::declval<Args>() ...));

template <typename T,typename ... Args>
using has_onplay_lrc_func_vt = wws::is_detected<has_onplay_lrc_func_t,T,Args...>;

template <class T,class ...Args>											
using has_playing_lrc_func_t = decltype(std::declval<T>().playing(std::declval<Args>() ...));

template <typename T,typename ... Args>
using has_playing_lrc_func_vt = wws::is_detected<has_playing_lrc_func_t,T,Args...>;

template <typename LOAD,typename Analyse,typename View>
struct LrcMgr
{
    using LRC_TYPE  = typename LOAD::LrcType;
    static_assert(has_load_lrc_func_vt<LOAD,std::string>::value,"Bad lrc loader type!!!");
    static_assert(has_analyse_lrc_func_vt<Analyse,std::shared_ptr<LRC_TYPE>,double,size_t>::value,"Bad lrc analyse type!!!");
    static_assert((has_onplay_lrc_func_vt<View,std::shared_ptr<LRC_TYPE>>::value && 
        has_playing_lrc_func_vt<View,size_t>::value),"Bad lrc view type!!!");
    void onplay(const MMFile& f)
    {
        
#ifdef PF_WIN32
        auto ws = std::wstring(f.getAbsolutePathNoSuffix());
        auto m = cvt::unicode2ansi(ws);
#else
        auto m = dbg(f.getAbsolutePathNoSuffix());
#endif // PF_WIN32

        m += ".lrc";
        curr = load.load(m);
        if(view) view->on_play(curr);
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
            if(view) view->playing(idx);
            //printf("%lf : %s\n",curr->data[idx].second, curr->data[idx].line.c_str() );
        }
            
        last_idx = idx;
    }
    void attach_view(std::shared_ptr<View> view)
    {
        this->view = std::move(view);
        if(curr) this->view->on_play(curr);
    }
    LOAD load;
    Analyse analyse;
    std::shared_ptr<typename LOAD::LrcType> curr;
    std::shared_ptr<View> view;
    size_t last_idx = -1;
};

typedef LrcMgr<LrcLoader,LrcAnalyse,LrcView> DefLrcMgr;

}