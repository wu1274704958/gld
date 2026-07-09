#pragma once

// ECS-style windowing (Bevy-inspired). WindowPlugin creates a GLFW window +
// GL context, wires window/input callbacks into ECS Events, and exposes a
// Window resource. run_app() owns the main loop (poll -> tick -> swap ->
// frame_end). Resize/mouse/keyboard all flow through ECS events/resources.

#include <string>
#include <glm/glm.hpp>
#include "App.hpp"

struct GLFWwindow;

namespace gld::ecs {

    struct Window {
        GLFWwindow* handle = nullptr;
        int width = 0;
        int height = 0;
        std::string title;
        bool should_close = false;
        glm::vec2 last_cursor{0.f};
        bool has_cursor = false;
    };

    // Plugin (holds desired window params). Use: app.add_plugin(WindowPlugin{w,h,"title"}).
    struct WindowPlugin {
        int width = 1280;
        int height = 720;
        std::string title = "ecs";
        void operator()(App& app) const;
    };

    // Reads WindowResized events -> glViewport + Window size (PreUpdate).
    void window_resize_system(EcsWorld& w);

    // Windowed main loop: Startup once, then poll/tick/swap/frame_end until close.
    void run_app(App& app);
}
