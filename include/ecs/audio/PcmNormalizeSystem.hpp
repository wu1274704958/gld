#pragma once

#include "AudioComponents.hpp"
#include "../App.hpp"
#include "../EcsWorld.hpp"

namespace gld::ecs {

    void pcm_normalize_system(EcsWorld& w);
    void PcmNormalizePlugin(App& app);
}
