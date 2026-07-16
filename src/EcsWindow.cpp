#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <ecs/Window.hpp>
#include <ecs/Events.hpp>
#include <ecs/Input.hpp>
#include <ecs/render/RenderSystem.hpp>
#include <ecs/render/BatchSystem.hpp>
#include <ecs/render/RenderTarget.hpp>
#include <ecs/render/PostProcess.hpp>
#include <ecs/render/Lighting.hpp>
#include <ecs/assets/AssetServer.hpp>
#include <ecs/assets/Assets.hpp>
#include <ecs/text/FontAsset.hpp>
#include <ecs/text/GlyphAtlas.hpp>

#include <program.hpp>
#include <texture.hpp>

#include <memory>

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
        if (auto* win = world.try_resource<Window>()) win->presented = false;
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

    static void cleanup_app_resources(App& app) {
        auto& world = app.world;

        cleanup_render_resources(world);

        if (auto* srv = world.try_resource<AssetServer>())
            srv->shutdown();

        world.reg().clear();

        world.remove_resource<BatchResources>();
        world.remove_resource<FullscreenResources>();
        world.remove_resource<LightingGpuResource>();
        world.remove_resource<LightingSettings>();
        world.remove_resource<PostProcessManager>();
        world.remove_resource<TextBatchIndex>();
        world.remove_resource<GlyphAtlasAA>();
        world.remove_resource<GlyphAtlasSDF>();
        world.remove_resource<std::shared_ptr<RenderTarget>>();

        world.remove_resource<AssetServer>();
        world.remove_resource<AssetManager>();
    }

    void run_app(App& app) {
        auto* win = app.world.try_resource<Window>();
        if (!win || !win->handle) return;
        GLFWwindow* handle = win->handle;

        while (!app.world.resource<Window>().should_close &&
               !glfwWindowShouldClose(handle)) {
            glfwPollEvents();
            app.tick();
            // present_system (if present) swaps and sets Window.presented; only
            // swap here when nothing already presented this frame (back-compat).
            if (!app.world.resource<Window>().presented)
                glfwSwapBuffers(handle);
            frame_end(app.world);
        }

        cleanup_app_resources(app);
        glfwDestroyWindow(handle);
        glfwTerminate();
    }
}
