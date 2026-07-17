#pragma once

// EcsWorld – thin wrapper over entt::registry providing the entity/component
// store plus a resource (context-variable) area. Resources are Bevy-style
// singletons living in the registry context.

#include <entt/entt.hpp>
#include <algorithm>
#include <vector>
#include <utility>

namespace gld::ecs {

    enum class ResourceCleanupPriority : int {
        Early = 0,
        Normal = 100,
        Late = 200,
        AssetServer = 900,
        AssetManager = 1000
    };

    struct EcsWorld {
        entt::registry registry;

        ~EcsWorld() {
            cleanup_resources();
        }

        entt::registry& reg() { return registry; }
        const entt::registry& reg() const { return registry; }

        // ---- entities ----
        entt::entity spawn() { return registry.create(); }
        void despawn(entt::entity e) { registry.destroy(e); }

        // ---- resources (context singletons) ----
        template<class R, class... Args>
        R& add_resource(Args&&... args) {
            return add_resource_with_priority<R>(static_cast<int>(ResourceCleanupPriority::Normal),
                                                std::forward<Args>(args)...);
        }
        template<class R, class... Args>
        R& add_resource_with_priority(int priority, Args&&... args) {
            R& res = registry.ctx().emplace<R>(std::forward<Args>(args)...);
            track_resource<R>(priority);
            return res;
        }
        template<class R>
        R& resource() { return registry.ctx().get<R>(); }
        template<class R>
        R* try_resource() { return registry.ctx().find<R>(); }
        template<class R>
        bool has_resource() { return registry.ctx().contains<R>(); }
        template<class R>
        bool remove_resource() { return registry.ctx().erase<R>(); }
        template<class R, class... Args>
        R& resource_or_add(Args&&... args) {
            return resource_or_add_with_priority<R>(static_cast<int>(ResourceCleanupPriority::Normal),
                                                   std::forward<Args>(args)...);
        }
        template<class R, class... Args>
        R& resource_or_add_with_priority(int priority, Args&&... args) {
            if (auto* p = registry.ctx().find<R>()) {
                track_resource<R>(priority);
                return *p;
            }
            R& res = registry.ctx().emplace<R>(std::forward<Args>(args)...);
            track_resource<R>(priority);
            return res;
        }

        void cleanup_resources() {
            if (cleaned_up_) return;
            cleaned_up_ = true;

            registry.clear();

            std::stable_sort(cleanup_.begin(), cleanup_.end(),
                [](const ResourceCleanupEntry& a, const ResourceCleanupEntry& b) {
                    return a.priority < b.priority;
                });

            for (auto& entry : cleanup_)
                entry.erase(registry);
            cleanup_.clear();
        }

    private:
        struct ResourceCleanupEntry {
            entt::id_type type = 0;
            int priority = static_cast<int>(ResourceCleanupPriority::Normal);
            bool (*erase)(entt::registry&) = nullptr;
        };

        template<class R>
        void track_resource(int priority) {
            const entt::id_type type = entt::type_id<R>().hash();
            auto it = std::find_if(cleanup_.begin(), cleanup_.end(),
                [&](const ResourceCleanupEntry& entry) { return entry.type == type; });
            if (it != cleanup_.end()) {
                it->priority = priority;
                return;
            }

            cleanup_.push_back(ResourceCleanupEntry{
                type,
                priority,
                [](entt::registry& reg) { return reg.ctx().erase<R>(); }
            });
        }

        std::vector<ResourceCleanupEntry> cleanup_;
        bool cleaned_up_ = false;
    };
}
