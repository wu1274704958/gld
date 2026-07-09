#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <ecs/Window.hpp>
#include <ecs/Events.hpp>
#include <ecs/Input.hpp>

namespace gld::ecs {

    static EcsWorld* world_of(GLFWwindow* w) {
        return static_cast<EcsWorld*>(glfwGetWindowUserPointer(w));
    }

    void window_resize_system(EcsWorld& world) {
        auto* ev = world.try_resource<Events<WindowResized>>();
        if (!ev || ev->empty()) return;
        const auto& last = ev->read().back();
        glViewport(0, 0, last.width, last.height);
        if (auto* win = world.try_resource<Window>()) {
            win->width = last.width;
            win->height = last.height;
        }
    }

    void WindowPlugin::operator()(App& app) const {
        if (!glfwInit()) return;

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        GLFWwindow* w = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
        if (!w) { glfwTerminate(); return; }
        glfwMakeContextCurrent(w);
        gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress));
        glfwSwapInterval(1);

        auto& win = app.world.resource_or_add<Window>();
        win.handle = w;
        win.width = width;
        win.height = height;
        win.title = title;
        win.should_close = false;

        // Ensure event resources exist so callbacks can push safely.
        app.world.resource_or_add<Events<KeyEvent>>();
        app.world.resource_or_add<Events<MouseButtonEvent>>();
        app.world.resource_or_add<Events<CursorMoved>>();
        app.world.resource_or_add<Events<WindowResized>>();
        app.world.resource_or_add<Events<ScrollEvent>>();
        app.world.resource_or_add<Events<CloseRequested>>();

        glfwSetWindowUserPointer(w, &app.world);

        glfwSetFramebufferSizeCallback(w, [](GLFWwindow* win, int fw, int fh) {
            auto* world = world_of(win);
            if (!world) return;
            world->resource_or_add<Events<WindowResized>>().emit(WindowResized{ fw, fh });
        });
        glfwSetCursorPosCallback(w, [](GLFWwindow* win, double x, double y) {
            auto* world = world_of(win);
            if (!world) return;
            auto& wr = world->resource<Window>();
            glm::vec2 p(static_cast<float>(x), static_cast<float>(y));
            glm::vec2 d = wr.has_cursor ? (p - wr.last_cursor) : glm::vec2(0.f);
            wr.last_cursor = p; wr.has_cursor = true;
            world->resource_or_add<Events<CursorMoved>>().emit(CursorMoved{ p, d });
        });
        glfwSetMouseButtonCallback(w, [](GLFWwindow* win, int button, int action, int mods) {
            auto* world = world_of(win);
            if (!world) return;
            world->resource_or_add<Events<MouseButtonEvent>>().emit(MouseButtonEvent{ button, action, mods });
        });
        glfwSetKeyCallback(w, [](GLFWwindow* win, int key, int sc, int action, int mods) {
            auto* world = world_of(win);
            if (!world) return;
            world->resource_or_add<Events<KeyEvent>>().emit(KeyEvent{ key, sc, action, mods });
        });
        glfwSetScrollCallback(w, [](GLFWwindow* win, double ox, double oy) {
            auto* world = world_of(win);
            if (!world) return;
            world->resource_or_add<Events<ScrollEvent>>().emit(
                ScrollEvent{ glm::vec2(static_cast<float>(ox), static_cast<float>(oy)) });
        });
        glfwSetWindowCloseCallback(w, [](GLFWwindow* win) {
            auto* world = world_of(win);
            if (!world) return;
            world->resource<Window>().should_close = true;
            world->resource_or_add<Events<CloseRequested>>().emit(CloseRequested{});
        });

        app.add_system(Stage::PreUpdate, window_resize_system);
    }

    static void frame_end(EcsWorld& world) {
        if (auto* kb = world.try_resource<Keyboard>()) kb->clear_transitions();
        if (auto* mb = world.try_resource<MouseButtons>()) mb->clear_transitions();
        if (auto* cur = world.try_resource<CursorPosition>()) cur->delta = glm::vec2(0.f);
        if (auto* e = world.try_resource<Events<KeyEvent>>()) e->clear();
        if (auto* e = world.try_resource<Events<MouseButtonEvent>>()) e->clear();
        if (auto* e = world.try_resource<Events<CursorMoved>>()) e->clear();
        if (auto* e = world.try_resource<Events<WindowResized>>()) e->clear();
        if (auto* e = world.try_resource<Events<ScrollEvent>>()) e->clear();
        if (auto* e = world.try_resource<Events<CloseRequested>>()) e->clear();
    }

    void run_app(App& app) {
        auto* win = app.world.try_resource<Window>();
        if (!win || !win->handle) return;
        GLFWwindow* handle = win->handle;

        while (!app.world.resource<Window>().should_close &&
               !glfwWindowShouldClose(handle)) {
            glfwPollEvents();
            app.tick();
            glfwSwapBuffers(handle);
            frame_end(app.world);
        }

        glfwDestroyWindow(handle);
        glfwTerminate();
    }
}
