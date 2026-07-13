#pragma once

#include "AudioComponents.hpp"
#include "../App.hpp"
#include "../EcsWorld.hpp"

namespace gld::ecs {

    void fft_system(EcsWorld& w);
    void FftPlugin(App& app);
}
