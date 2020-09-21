#pragma once
#include "comm.h"

namespace lrc{
    struct LrcAnalyse{
        using LrcType = Lrc;
        size_t analyse(std::shared_ptr<LrcType> lrc,double s,size_t last);
    };
}