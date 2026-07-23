#pragma once

#include "Aoe2Systems.hpp"
#include <ecs/App.hpp>

namespace gld::ecs::aoe2 {

struct Aoe2Plugin {
    std::string cache_root = "aoe2de_cache";
    void operator()(App& app) const;
};

} // namespace gld::ecs::aoe2
