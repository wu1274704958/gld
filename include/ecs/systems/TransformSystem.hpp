#pragma once

// Transform / core systems + plugins.

#include <functional>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include "../App.hpp"
#include "../BaseSystem.hpp"
#include "../Components.hpp"

namespace gld::ecs {

    inline glm::mat4 local_matrix(const Transform& t) {
        glm::mat4 m(1.f);
        m = glm::translate(m, t.translation);
        m = glm::rotate(m, t.rotation.x, glm::vec3(1.f, 0.f, 0.f));
        m = glm::rotate(m, t.rotation.y, glm::vec3(0.f, 1.f, 0.f));
        m = glm::rotate(m, t.rotation.z, glm::vec3(0.f, 0.f, 1.f));
        m = glm::scale(m, t.scale);
        return m;
    }

    // Hierarchical world-matrix propagation (roots first, DFS). This is
    // inherently ordered (parent before child) so it is a plain system rather
    // than a per-entity BaseSystem.
    inline void transform_propagate_system(EcsWorld& w) {
        auto& reg = w.reg();

        std::function<void(entt::entity, const glm::mat4&)> visit =
            [&](entt::entity e, const glm::mat4& parent_world) {
                glm::mat4 local(1.f);
                if (auto* t = reg.try_get<Transform>(e)) local = local_matrix(*t);
                glm::mat4 world = parent_world * local;

                if (auto* g = reg.try_get<GlobalTransform>(e)) {
                    if (g->world != world) { g->world = world; ++g->version; }
                } else {
                    reg.emplace<GlobalTransform>(e, GlobalTransform{world, 1});
                }

                if (auto* ch = reg.try_get<Children>(e))
                    for (auto c : ch->value) visit(c, world);
            };

        for (auto e : reg.view<Transform>()) {
            auto* p = reg.try_get<Parent>(e);
            if (!p || p->value == entt::null)
                visit(e, glm::mat4(1.f));
        }
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
        app.add_system(Stage::PostUpdate, transform_propagate_system);
    }
}
