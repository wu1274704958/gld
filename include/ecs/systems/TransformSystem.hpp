#pragma once

// Transform / core systems + plugins.

#include <chrono>
#include <functional>
#include <algorithm>
#include <unordered_set>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>
#include "../App.hpp"
#include "../BaseSystem.hpp"
#include "../Components.hpp"
#include "../PerformanceMonitoring.hpp"

namespace gld::ecs {

    struct TransformDiagnostics {
        double cpu_ms = 0.0;
        std::uint32_t visited = 0;
        std::uint32_t changed = 0;
        std::uint32_t queued = 0;
        std::uint32_t roots = 0;
        std::uint32_t coalesced = 0;
    };

    struct TransformDirtyQueue {
        std::vector<entt::entity> entities;
        std::unordered_set<entt::entity> unique;
    };

    struct TransformChanges {
        std::vector<entt::entity> entities;
        std::uint64_t frame = 0;
    };

    struct TransformLifecycle {
        std::uint64_t next_revision = 1;
        bool connected = false;
    };

    inline void enqueue_transform_dirty(entt::registry& registry, entt::entity entity) {
        auto& queue = registry.ctx().get<TransformDirtyQueue>();
        if (queue.unique.insert(entity).second) queue.entities.push_back(entity);
    }

    inline void on_transform_construct(entt::registry& registry, entt::entity entity) {
        auto& lifecycle = registry.ctx().get<TransformLifecycle>();
        TransformAccess::revision(registry.get<Transform>(entity), lifecycle.next_revision++);
        enqueue_transform_dirty(registry, entity);
    }

    inline void on_transform_update(entt::registry& registry, entt::entity entity) {
        auto& lifecycle = registry.ctx().get<TransformLifecycle>();
        TransformAccess::revision(registry.get<Transform>(entity), lifecycle.next_revision++);
        enqueue_transform_dirty(registry, entity);
    }

    inline void on_transform_destroy(entt::registry& registry, entt::entity entity) {
        enqueue_transform_dirty(registry, entity);
    }

    inline void on_parent_change(entt::registry& registry, entt::entity entity) {
        enqueue_transform_dirty(registry, entity);
    }

    inline void on_parent_destroy(entt::registry& registry, entt::entity entity) {
        const entt::entity parent = registry.get<Parent>(entity).value;
        if (registry.valid(parent)) {
            if (auto* children = registry.try_get<Children>(parent))
                std::erase(children->value, entity);
        }
        enqueue_transform_dirty(registry, entity);
    }

    inline void on_children_destroy(entt::registry& registry, entt::entity entity) {
        // Copy first: removing Parent below invokes on_parent_destroy, which
        // also repairs this Children list.
        const auto children = registry.get<Children>(entity).value;
        for (auto child : children) {
            if (!registry.valid(child)) continue;
            const auto* parent = registry.try_get<Parent>(child);
            if (parent && parent->value == entity) registry.remove<Parent>(child);
        }
    }

    inline void register_transform_lifecycle(EcsWorld& world) {
        auto& lifecycle = world.resource_or_add<TransformLifecycle>();
        world.resource_or_add<TransformDirtyQueue>();
        world.resource_or_add<TransformChanges>();
        if (lifecycle.connected) return;
        auto& registry = world.reg();
        registry.on_construct<Transform>().connect<&on_transform_construct>();
        registry.on_update<Transform>().connect<&on_transform_update>();
        registry.on_destroy<Transform>().connect<&on_transform_destroy>();
        registry.on_construct<Parent>().connect<&on_parent_change>();
        registry.on_update<Parent>().connect<&on_parent_change>();
        registry.on_destroy<Parent>().connect<&on_parent_destroy>();
        registry.on_destroy<Children>().connect<&on_children_destroy>();
        lifecycle.connected = true;
        for (auto entity : registry.view<Transform>()) {
            TransformAccess::revision(registry.get<Transform>(entity), lifecycle.next_revision++);
            enqueue_transform_dirty(registry, entity);
        }
    }

    inline void disconnect_transform_lifecycle(EcsWorld& world) {
        auto* lifecycle = world.try_resource<TransformLifecycle>();
        if (!lifecycle || !lifecycle->connected) return;
        auto& registry = world.reg();
        registry.on_construct<Transform>().disconnect<&on_transform_construct>();
        registry.on_update<Transform>().disconnect<&on_transform_update>();
        registry.on_destroy<Transform>().disconnect<&on_transform_destroy>();
        registry.on_construct<Parent>().disconnect<&on_parent_change>();
        registry.on_update<Parent>().disconnect<&on_parent_change>();
        registry.on_destroy<Parent>().disconnect<&on_parent_destroy>();
        registry.on_destroy<Children>().disconnect<&on_children_destroy>();
        lifecycle->connected = false;
    }

    template<class Function>
    bool patch_transform(EcsWorld& world, entt::entity entity, Function&& function) {
        auto& registry = world.reg();
        const auto* current = registry.try_get<Transform>(entity);
        if (!current) return false;
        Transform next = *current;
        TransformEditor editor(next);
        std::forward<Function>(function)(editor);
        if (!editor.changed()) return false;
        registry.replace<Transform>(entity, std::move(next));
        return true;
    }

    inline bool set_parent(EcsWorld& world, entt::entity child, entt::entity parent) {
        auto& registry = world.reg();
        if (!registry.valid(child) || !registry.valid(parent) || child == parent) return false;
        for (entt::entity cursor = parent; cursor != entt::null;) {
            if (cursor == child) return false;
            const auto* ancestor = registry.try_get<Parent>(cursor);
            cursor = ancestor ? ancestor->value : entt::null;
        }
        if (const auto* old = registry.try_get<Parent>(child); old && old->value == parent)
            return true;
        if (const auto* old = registry.try_get<Parent>(child);
            old && registry.valid(old->value)) {
            if (auto* children = registry.try_get<Children>(old->value))
                std::erase(children->value, child);
        }
        registry.emplace_or_replace<Parent>(child, Parent{parent});
        auto& children = registry.get_or_emplace<Children>(parent).value;
        if (std::find(children.begin(), children.end(), child) == children.end())
            children.push_back(child);
        enqueue_transform_dirty(registry, child);
        return true;
    }

    inline bool clear_parent(EcsWorld& world, entt::entity child) {
        auto& registry = world.reg();
        const auto* parent = registry.try_get<Parent>(child);
        if (!parent) return false;
        if (registry.valid(parent->value)) {
            if (auto* children = registry.try_get<Children>(parent->value))
                std::erase(children->value, child);
        }
        registry.remove<Parent>(child);
        enqueue_transform_dirty(registry, child);
        return true;
    }

    inline glm::mat4 local_matrix(const Transform& t) {
        glm::mat4 m(1.f);
        m = glm::translate(m, t.translation());
        m = glm::rotate(m, t.rotation().x, glm::vec3(1.f, 0.f, 0.f));
        m = glm::rotate(m, t.rotation().y, glm::vec3(0.f, 1.f, 0.f));
        m = glm::rotate(m, t.rotation().z, glm::vec3(0.f, 0.f, 1.f));
        m = glm::scale(m, t.scale());
        return m;
    }

    // Hierarchical world-matrix propagation (roots first, DFS). This is
    // inherently ordered (parent before child) so it is a plain system rather
    // than a per-entity BaseSystem.
    inline void transform_propagate_system(EcsWorld& w) {
        GLD_PERF_TIME_POINT(started);
        auto& diagnostics = w.resource_or_add<TransformDiagnostics>();
        GLD_PERF_MONITOR(diagnostics = {});
        auto& reg = w.reg();
        auto& queue = w.resource_or_add<TransformDirtyQueue>();
        auto& changes = w.resource_or_add<TransformChanges>();
        changes.entities.clear();
        ++changes.frame;
        GLD_PERF_MONITOR(
            diagnostics.queued = static_cast<std::uint32_t>(queue.entities.size());
        );
        std::vector<entt::entity> pending = std::move(queue.entities);
        queue.entities.clear();
        queue.unique.clear();
        std::unordered_set<entt::entity> dirty;
        for (auto entity : pending) if (reg.valid(entity)) dirty.insert(entity);

        std::vector<entt::entity> roots;
        for (auto entity : pending) {
            if (!reg.valid(entity)) continue;
            bool covered = false;
            std::unordered_set<entt::entity> ancestry_guard;
            const Parent* parent = reg.try_get<Parent>(entity);
            entt::entity cursor = parent ? parent->value : entt::null;
            while (cursor != entt::null && reg.valid(cursor) &&
                   ancestry_guard.insert(cursor).second) {
                if (dirty.contains(cursor)) { covered = true; break; }
                const Parent* next = reg.try_get<Parent>(cursor);
                cursor = next ? next->value : entt::null;
            }
            if (covered) GLD_PERF_MONITOR(++diagnostics.coalesced);
            else roots.push_back(entity);
        }
        GLD_PERF_MONITOR(
            diagnostics.roots = static_cast<std::uint32_t>(roots.size());
        );
        std::unordered_set<entt::entity> visited;
        std::function<void(entt::entity, const glm::mat4&)> visit =
            [&](entt::entity e, const glm::mat4& parent_world) {
                if (!reg.valid(e) || !visited.insert(e).second) return;
                GLD_PERF_MONITOR(++diagnostics.visited);
                glm::mat4 local(1.f);
                if (auto* t = reg.try_get<Transform>(e)) local = local_matrix(*t);
                glm::mat4 world = parent_world * local;

                if (auto* g = reg.try_get<GlobalTransform>(e)) {
                    if (g->world != world) {
                        g->world = world;
                        ++g->version;
                        GLD_PERF_MONITOR(++diagnostics.changed);
                        changes.entities.push_back(e);
                    }
                } else {
                    reg.emplace<GlobalTransform>(e, GlobalTransform{world, 1});
                    GLD_PERF_MONITOR(++diagnostics.changed);
                    changes.entities.push_back(e);
                }

                if (auto* ch = reg.try_get<Children>(e))
                    for (auto c : ch->value) visit(c, world);
            };

        for (auto e : roots) {
            glm::mat4 parent_world(1.f);
            if (const auto* parent = reg.try_get<Parent>(e);
                parent && reg.valid(parent->value)) {
                if (const auto* global = reg.try_get<GlobalTransform>(parent->value))
                    parent_world = global->world;
            }
            visit(e, parent_world);
        }
        GLD_PERF_MONITOR(
            diagnostics.cpu_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
        );
    }

    // BaseSystem demonstration: computes a local-only world matrix. Validates
    // the templated Query/forward path (queries Transform + GlobalTransform,
    // both declared in the CTS list). Useful for flat (non-hierarchical) nodes.
    struct LocalToWorldSystem : BaseSystem<LocalToWorldSystem, Transform, GlobalTransform> {
        void Update(entt::entity, Transform& t, GlobalTransform& g) {
            glm::mat4 world = local_matrix(t);
            if (g.world != world) { g.world = world; ++g.version; }
        }
    };

    inline void update_time(Time& time, TimeClock& clock, const TimeSettings& settings,
                            TimeClock::Clock::time_point now) {
        if (!clock.initialized) {
            clock.previous = now;
            clock.initialized = true;
            time.dt = 0.f;
            time.raw_dt = 0.f;
            ++time.frame;
            return;
        }
        const float raw = std::max(0.f,
            std::chrono::duration<float>(now - clock.previous).count());
        clock.previous = now;
        time.raw_dt = raw;
        time.dt = std::clamp(raw, 0.f, std::max(0.f, settings.max_delta));
        time.wall_elapsed += raw;
        time.elapsed += time.dt;
        ++time.frame;
        if (raw > 0.f) {
            const float instant = 1.f / raw;
            time.fps = time.fps <= 0.f ? instant : time.fps + (instant - time.fps) * 0.08f;
        }
    }
    inline void time_system(EcsWorld& w) {
        auto& time = w.resource_or_add<Time>();
        auto& clock = w.resource_or_add<TimeClock>();
        const auto& settings = w.resource_or_add<TimeSettings>();
        update_time(time, clock, settings, TimeClock::Clock::now());
    }

    // ---- plugins ----
    inline void CorePlugin(App& app) {
        app.world.resource_or_add<Time>();
        app.world.resource_or_add<TimeClock>();
        app.world.resource_or_add<TimeSettings>();
        app.add_system(Stage::First, time_system);
    }
    inline void TransformPlugin(App& app) {
        register_transform_lifecycle(app.world);
        app.add_system(Stage::PostUpdate, transform_propagate_system);
        app.add_system(Stage::Shutdown, disconnect_transform_lifecycle);
    }
}
