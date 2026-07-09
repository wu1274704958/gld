#pragma once

// ECS input resources (Bevy-style). ButtonInput<Btn> tracks pressed / just
// pressed / just released. CursorPosition holds the cursor and its per-frame
// delta. input_apply_system translates the frame's raw window events (KeyEvent,
// MouseButtonEvent, CursorMoved) into these resources during PreUpdate.

#include <unordered_set>
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

#include "EcsWorld.hpp"
#include "App.hpp"
#include "Events.hpp"

namespace gld::ecs {

    template<class Btn>
    struct ButtonInput {
        std::unordered_set<Btn> pressed;
        std::unordered_set<Btn> just_pressed;
        std::unordered_set<Btn> just_released;

        bool is_pressed(Btn b) const { return pressed.count(b) != 0; }
        bool just_now_pressed(Btn b) const { return just_pressed.count(b) != 0; }
        bool just_now_released(Btn b) const { return just_released.count(b) != 0; }

        void press(Btn b) {
            if (pressed.insert(b).second) just_pressed.insert(b);
        }
        void release(Btn b) {
            if (pressed.erase(b)) just_released.insert(b);
        }
        void clear_transitions() { just_pressed.clear(); just_released.clear(); }
    };

    // Keyboard keys and mouse buttons are raw GLFW ints for v1.
    using Keyboard = ButtonInput<int>;
    using MouseButtons = ButtonInput<int>;

    struct CursorPosition {
        glm::vec2 position{0.f};
        glm::vec2 delta{0.f};
    };

    // Apply this frame's raw events to the input resources (PreUpdate).
    inline void input_apply_system(EcsWorld& w) {
        auto& kb = w.resource_or_add<Keyboard>();
        auto& mb = w.resource_or_add<MouseButtons>();
        auto& cur = w.resource_or_add<CursorPosition>();

        if (auto* ke = w.try_resource<Events<KeyEvent>>()) {
            for (auto& e : ke->read()) {
                if (e.action == GLFW_PRESS) kb.press(e.key);
                else if (e.action == GLFW_RELEASE) kb.release(e.key);
            }
        }
        if (auto* me = w.try_resource<Events<MouseButtonEvent>>()) {
            for (auto& e : me->read()) {
                if (e.action == GLFW_PRESS) mb.press(e.button);
                else if (e.action == GLFW_RELEASE) mb.release(e.button);
            }
        }
        if (auto* cm = w.try_resource<Events<CursorMoved>>()) {
            cur.delta = glm::vec2(0.f);
            for (auto& e : cm->read()) {
                cur.delta += e.delta;
                cur.position = e.position;
            }
        }
    }

    // Registers input resources + the apply system. WindowPlugin wires callbacks.
    inline void InputPlugin(App& app) {
        app.world.resource_or_add<Keyboard>();
        app.world.resource_or_add<MouseButtons>();
        app.world.resource_or_add<CursorPosition>();
        app.world.resource_or_add<Events<KeyEvent>>();
        app.world.resource_or_add<Events<MouseButtonEvent>>();
        app.world.resource_or_add<Events<CursorMoved>>();
        app.add_system(Stage::PreUpdate, input_apply_system);
    }
}
