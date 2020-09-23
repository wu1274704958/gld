#pragma once
#include "comm.h"
#include <memory>

namespace lrc{
    struct LrcAnalyse{
        using LrcType = Lrc;
        size_t analyse(std::shared_ptr<LrcType> lrc,double s,size_t last);
    };
}