#pragma once

// BaseSystem – templated System base. The template parameter pack CTS... is the
// set of component types this system is ALLOWED to query. A derived system just
// defines:
//
//     void Update(entt::entity e, CompA& a, CompB& b, ...);
//
// The first parameter MUST be entt::entity; every following component type must
// be one of CTS... (validated at compile time via a C++20 concept). BaseSystem
// deduces those component types from the Update signature, builds the matching
// entt view, iterates it and forwards each entity to Update. The query logic is
// therefore fully encapsulated here.
//
//     struct MoveSystem : BaseSystem<MoveSystem, Transform, GlobalTransform> {
//         void Update(entt::entity e, Transform& t, GlobalTransform& g) { ... }
//     };

#include <entt/entt.hpp>
#include <type_traits>
#include <concepts>
#include "EcsWorld.hpp"

namespace gld::ecs {

    // A queried Update parameter (after entity) must resolve to one of CTS...
    template<class T, class... CTS>
    concept ComponentIn = (std::same_as<std::remove_cvref_t<T>, CTS> || ...);

    template<class Derived, class... CTS>
    struct BaseSystem {
        void run(EcsWorld& world) { world_ = &world; dispatch(world, &Derived::Update); }

    protected:
        // Resource access for the derived Update (e.g. res<Time>().dt).
        EcsWorld& world() { return *world_; }
        template<class R> R& res() { return world_->resource<R>(); }
        template<class R> R* try_res() { return world_->try_resource<R>(); }

    private:
        template<class R, class... Ps>
        void dispatch(EcsWorld& world, R (Derived::*fn)(entt::entity, Ps...))
            requires (ComponentIn<Ps, CTS...> && ...)
        {
            auto view = world.reg().template view<std::remove_cvref_t<Ps>...>();
            for (auto e : view) {
                (static_cast<Derived*>(this)->*fn)(
                    e, view.template get<std::remove_cvref_t<Ps>>(e)...);
            }
        }

        EcsWorld* world_ = nullptr;
    };
}
