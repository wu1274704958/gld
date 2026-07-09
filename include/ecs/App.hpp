#pragma once

// App – Bevy-style staged scheduler over an EcsWorld. Systems are plain
// callables void(EcsWorld&) or BaseSystem-derived objects (added by type).
// Plugins are functions that register systems/resources into the App.
//
// This phase is a pure logic scheduler (no window / no GL). run()/tick() execute
// the per-frame stages in order; Startup runs once on the first tick.

#include <array>
#include <vector>
#include <memory>
#include <functional>
#include "EcsWorld.hpp"

namespace gld::ecs {

    enum class Stage {
        Startup,     // once, before first frame
        First,
        PreUpdate,
        Update,
        PostUpdate,
        Render,
        Last,
        COUNT
    };

    class App {
    public:
        EcsWorld world;
        using SysFn = std::function<void(EcsWorld&)>;

        // Register a plain system function into a stage.
        App& add_system(Stage s, SysFn fn) {
            stages_[static_cast<size_t>(s)].push_back(std::move(fn));
            return *this;
        }

        // Register a BaseSystem-derived system by type; its run() is invoked.
        template<class Sys, class... Args>
        App& add_system(Stage s, Args&&... args) {
            auto sys = std::make_shared<Sys>(std::forward<Args>(args)...);
            keep_alive_.push_back(sys);
            stages_[static_cast<size_t>(s)].push_back(
                [sys](EcsWorld& w) { sys->run(w); });
            return *this;
        }

        // A plugin is any callable that configures the App.
        App& add_plugin(const std::function<void(App&)>& plugin) {
            plugin(*this);
            return *this;
        }

        void tick() {
            if (!started_) { run_stage(Stage::Startup); started_ = true; }
            run_stage(Stage::First);
            run_stage(Stage::PreUpdate);
            run_stage(Stage::Update);
            run_stage(Stage::PostUpdate);
            run_stage(Stage::Render);
            run_stage(Stage::Last);
        }

        void run(int frames) { for (int i = 0; i < frames; ++i) tick(); }

        entt::entity spawn() { return world.spawn(); }

    private:
        void run_stage(Stage s) {
            for (auto& fn : stages_[static_cast<size_t>(s)]) fn(world);
        }

        std::array<std::vector<SysFn>, static_cast<size_t>(Stage::COUNT)> stages_;
        std::vector<std::shared_ptr<void>> keep_alive_;
        bool started_ = false;
    };
}
