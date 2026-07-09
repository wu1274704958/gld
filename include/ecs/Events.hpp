#pragma once

// ECS event system (Bevy-style). Events<E> is a per-type queue resource. Systems
// send events (typically window/input callbacks) and read them the same frame;
// the window main loop clears them at frame end.

#include <vector>
#include <glm/glm.hpp>

namespace gld::ecs {

    template<class E>
    struct Events {
        std::vector<E> queue;

        void send(const E& e) { queue.push_back(e); }
        template<class... Args> void emit(Args&&... args) { queue.emplace_back(std::forward<Args>(args)...); }
        const std::vector<E>& read() const { return queue; }
        bool empty() const { return queue.empty(); }
        void clear() { queue.clear(); }
    };

    // ---- window / input event types ----
    struct WindowResized { int width = 0; int height = 0; };
    struct CursorMoved   { glm::vec2 position{0.f}; glm::vec2 delta{0.f}; };
    struct MouseButtonEvent { int button = 0; int action = 0; int mods = 0; };
    struct KeyEvent      { int key = 0; int scancode = 0; int action = 0; int mods = 0; };
    struct ScrollEvent   { glm::vec2 offset{0.f}; };
    struct CloseRequested {};
}
