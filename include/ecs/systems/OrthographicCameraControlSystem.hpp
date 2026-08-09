#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../App.hpp"
#include "../Events.hpp"
#include "../render/RenderComponents.hpp"
#include "../Input.hpp"
#include "../Window.hpp"

namespace gld::ecs {

// Global input tuning shared by every controlled orthographic camera.
struct OrthographicCameraControlConfig {
    float pan_speed = 1.f;
    float zoom_speed = .15f;
    float minimum_zoom = .25f;
    float maximum_zoom = 8.f;
    int pan_button = GLFW_MOUSE_BUTTON_MIDDLE;
};

// Per-camera state. fit_center/fit_scale describe the scene's default frame;
// pan_offset/zoom preserve the user's navigation relative to that frame.
struct OrthographicCameraControl {
    glm::vec2 fit_center{0.f};
    float fit_scale = 1.f;
    glm::vec2 pan_offset{0.f};
    float zoom = 1.f;
    bool initialized = false;
    bool enabled = true;
};

inline bool finite_camera_value(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

inline void set_orthographic_camera_fit(
    OrthographicCameraControl& control, glm::vec2 center, float scale) {
    if (!finite_camera_value(center) || !std::isfinite(scale) || scale <= 0.f)
        return;
    control.fit_center = center;
    control.fit_scale = scale;
    if (!control.initialized) {
        control.pan_offset = {0.f, 0.f};
        control.zoom = 1.f;
        control.initialized = true;
    }
}

inline void reset_orthographic_camera_control(
    OrthographicCameraControl& control) {
    control.pan_offset = {0.f, 0.f};
    control.zoom = 1.f;
}

inline void orthographic_camera_control_system(EcsWorld& world) {
    auto& reg = world.reg();
    const auto& config =
        world.resource_or_add<OrthographicCameraControlConfig>();
    const auto* mouse = world.try_resource<MouseButtons>();
    const auto* cursor = world.try_resource<CursorPosition>();
    const auto* window = world.try_resource<Window>();

    glm::vec2 scroll{0.f};
    if (const auto* events = world.try_resource<Events<ScrollEvent>>())
        for (const auto& event : events->read())
            if (finite_camera_value(event.offset)) scroll += event.offset;

    const float pan_speed = std::max(0.f, config.pan_speed);
    const float zoom_speed = std::max(0.f, config.zoom_speed);
    const float minimum_zoom = std::max(.0001f, config.minimum_zoom);
    const float maximum_zoom = std::max(minimum_zoom, config.maximum_zoom);
    const bool dragging = mouse && cursor &&
        mouse->is_pressed(config.pan_button) &&
        finite_camera_value(cursor->delta);

    for (const auto entity : reg.view<Camera, OrthographicCameraControl>()) {
        auto& camera = reg.get<Camera>(entity);
        auto& control = reg.get<OrthographicCameraControl>(entity);
        if (camera.kind != CameraKind::Ortho || !control.enabled ||
            !control.initialized || !finite_camera_value(control.fit_center) ||
            !finite_camera_value(control.pan_offset) ||
            !std::isfinite(control.fit_scale) || control.fit_scale <= 0.f)
            continue;

        control.zoom = std::clamp(
            std::isfinite(control.zoom) ? control.zoom : 1.f,
            minimum_zoom, maximum_zoom);
        float scale = control.fit_scale * control.zoom;

        // GLFW cursor Y grows downwards while camera world Y grows upwards.
        // Moving the camera center oppositely makes the map follow the drag.
        if (dragging && scale > 0.f)
            control.pan_offset += glm::vec2{
                -cursor->delta.x, cursor->delta.y} * (pan_speed / scale);

        if (scroll.y != 0.f && zoom_speed > 0.f) {
            const float old_scale = scale;
            const float next_zoom = std::clamp(
                control.zoom * std::exp(scroll.y * zoom_speed),
                minimum_zoom, maximum_zoom);
            const float next_scale = control.fit_scale * next_zoom;
            if (window && window->has_cursor && cursor && old_scale > 0.f &&
                next_scale > 0.f && window->width > 0 && window->height > 0) {
                const glm::vec2 screen{
                    cursor->position.x - static_cast<float>(window->width) * .5f,
                    static_cast<float>(window->height) * .5f -
                        cursor->position.y};
                // Preserve the world point currently under the cursor.
                control.pan_offset += screen *
                    (1.f / old_scale - 1.f / next_scale);
            }
            control.zoom = next_zoom;
            scale = next_scale;
        }

        const glm::vec2 center = control.fit_center + control.pan_offset;
        glm::mat4 view{1.f};
        view = glm::scale(view, glm::vec3(scale, scale, 1.f));
        view = glm::translate(view, glm::vec3(-center, 0.f));
        camera.view = view;
    }
}

inline void OrthographicCameraControlPlugin(App& app) {
    app.world.resource_or_add<OrthographicCameraControlConfig>();
    app.add_system(Stage::Update, orthographic_camera_control_system);
}

} // namespace gld::ecs
