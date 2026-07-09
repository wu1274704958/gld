#pragma once

// EcsWorld – thin wrapper over entt::registry providing the entity/component
// store plus a resource (context-variable) area. Resources are Bevy-style
// singletons living in the registry context.

#include <entt/entt.hpp>
#include <utility>

namespace gld::ecs {

    struct EcsWorld {
        entt::registry registry;

        entt::registry& reg() { return registry; }
        const entt::registry& reg() const { return registry; }

        // ---- entities ----
        entt::entity spawn() { return registry.create(); }
        void despawn(entt::entity e) { registry.destroy(e); }

        // ---- resources (context singletons) ----
        template<class R, class... Args>
        R& add_resource(Args&&... args) {
            return registry.ctx().emplace<R>(std::forward<Args>(args)...);
        }
        template<class R>
        R& resource() { return registry.ctx().get<R>(); }
        template<class R>
        R* try_resource() { return registry.ctx().find<R>(); }
        template<class R>
        bool has_resource() { return registry.ctx().contains<R>(); }
        template<class R, class... Args>
        R& resource_or_add(Args&&... args) {
            if (auto* p = registry.ctx().find<R>()) return *p;
            return registry.ctx().emplace<R>(std::forward<Args>(args)...);
        }
    };
}
