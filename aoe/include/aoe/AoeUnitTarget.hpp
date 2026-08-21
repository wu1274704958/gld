#pragma once

#include <cstdint>

#include <entt/entity/entity.hpp>

namespace gld::ecs::aoe {

struct AoeUnitTarget {
    entt::entity entity{entt::null};
    std::uint64_t instance_id = 0;
};

} // namespace gld::ecs::aoe
