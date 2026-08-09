#include <cstdlib>
#include <cmath>
#include <vector>

#include <ecs/EcsWorld.hpp>
#include <ecs/systems/OrthographicCameraControlSystem.hpp>

using namespace gld::ecs;

namespace {

void require(bool condition) {
    if (!condition)
        std::abort();
}

void require_near(float actual, float expected, float epsilon = 1e-4f) {
    require(std::abs(actual - expected) <= epsilon);
}

struct LifetimeOwner {
    bool* alive = nullptr;
    std::vector<int>* destruction_order = nullptr;
    int marker = 0;

    LifetimeOwner(bool* alive_value, std::vector<int>* order, int marker_value)
        : alive(alive_value), destruction_order(order), marker(marker_value) {}

    ~LifetimeOwner() {
        *alive = false;
        destruction_order->push_back(marker);
    }
};

struct LifetimeDependent {
    const bool* owner_alive = nullptr;
    std::vector<int>* destruction_order = nullptr;
    int marker = 0;

    LifetimeDependent(const bool* alive, std::vector<int>* order, int marker_value)
        : owner_alive(alive), destruction_order(order), marker(marker_value) {}

    ~LifetimeDependent() {
        require(*owner_alive);
        destruction_order->push_back(marker);
    }
};

void plain_resource_or_add_preserves_priority() {
    bool owner_alive = true;
    std::vector<int> destruction_order;
    EcsWorld world;

    auto& owner = world.resource_or_add_with_priority<LifetimeOwner>(
        static_cast<int>(ResourceCleanupPriority::AssetManager),
        &owner_alive, &destruction_order, 2);
    auto& same_owner = world.resource_or_add<LifetimeOwner>(
        &owner_alive, &destruction_order, 99);
    require(&same_owner == &owner);
    require(same_owner.marker == 2);

    world.resource_or_add<LifetimeDependent>(
        &owner_alive, &destruction_order, 1);
    world.cleanup_resources();

    require((destruction_order == std::vector<int>{1, 2}));
    require(!owner_alive);

    // Cleanup is intentionally idempotent.
    world.cleanup_resources();
    require((destruction_order == std::vector<int>{1, 2}));
}

void explicit_priority_can_update_existing_resource() {
    bool owner_alive = true;
    std::vector<int> destruction_order;
    EcsWorld world;

    auto& owner = world.resource_or_add<LifetimeOwner>(
        &owner_alive, &destruction_order, 2);
    auto& reprioritized = world.resource_or_add_with_priority<LifetimeOwner>(
        static_cast<int>(ResourceCleanupPriority::AssetManager),
        &owner_alive, &destruction_order, 99);
    require(&reprioritized == &owner);
    require(reprioritized.marker == 2);

    world.resource_or_add<LifetimeDependent>(
        &owner_alive, &destruction_order, 1);
    world.cleanup_resources();

    require((destruction_order == std::vector<int>{1, 2}));
    require(!owner_alive);
}

void orthographic_camera_pan_zoom_and_reset() {
    EcsWorld world;
    auto& config = world.resource_or_add<OrthographicCameraControlConfig>();
    config.pan_speed = 1.f;
    config.zoom_speed = std::log(2.f);
    config.minimum_zoom = .5f;
    config.maximum_zoom = 4.f;

    auto& window = world.resource_or_add<Window>();
    window.width = 1000;
    window.height = 800;
    window.has_cursor = true;
    auto& mouse = world.resource_or_add<MouseButtons>();
    mouse.press(GLFW_MOUSE_BUTTON_MIDDLE);
    auto& cursor = world.resource_or_add<CursorPosition>();
    cursor.position = {750.f, 300.f};
    cursor.delta = {20.f, -10.f};
    world.resource_or_add<Events<ScrollEvent>>();

    const auto camera_entity = world.spawn();
    auto& camera = world.reg().emplace<Camera>(camera_entity);
    camera.kind = CameraKind::Ortho;
    auto& control = world.reg().emplace<OrthographicCameraControl>(camera_entity);
    set_orthographic_camera_fit(control, {10.f, 20.f}, 2.f);

    orthographic_camera_control_system(world);
    require_near(control.pan_offset.x, -10.f);
    require_near(control.pan_offset.y, -5.f);
    require_near(control.zoom, 1.f);

    mouse.release(GLFW_MOUSE_BUTTON_MIDDLE);
    cursor.delta = {0.f, 0.f};
    world.resource<Events<ScrollEvent>>().emit(ScrollEvent{{0.f, 1.f}});
    const glm::vec2 center_before = control.fit_center + control.pan_offset;
    const glm::vec2 screen{250.f, 100.f};
    const glm::vec2 world_under_cursor = center_before + screen / 2.f;
    orthographic_camera_control_system(world);
    require_near(control.zoom, 2.f);
    const glm::vec2 center_after = control.fit_center + control.pan_offset;
    require_near((center_after + screen / 4.f).x, world_under_cursor.x);
    require_near((center_after + screen / 4.f).y, world_under_cursor.y);

    world.resource<Events<ScrollEvent>>().clear();
    world.resource<Events<ScrollEvent>>().emit(ScrollEvent{{0.f, 100.f}});
    orthographic_camera_control_system(world);
    require_near(control.zoom, 4.f);

    reset_orthographic_camera_control(control);
    require_near(control.pan_offset.x, 0.f);
    require_near(control.pan_offset.y, 0.f);
    require_near(control.zoom, 1.f);

    // A marked perspective camera and an unmarked camera ignore the system.
    camera.kind = CameraKind::Perspective;
    control.pan_offset = {3.f, 4.f};
    world.resource<Events<ScrollEvent>>().clear();
    world.resource<Events<ScrollEvent>>().emit(ScrollEvent{{0.f, -1.f}});
    orthographic_camera_control_system(world);
    require(control.pan_offset == glm::vec2(3.f, 4.f));
    require_near(control.zoom, 1.f);
    const auto unmarked = world.spawn();
    auto& unmarked_camera = world.reg().emplace<Camera>(unmarked);
    const glm::mat4 unchanged = unmarked_camera.view;
    orthographic_camera_control_system(world);
    require(unmarked_camera.view == unchanged);
}

} // namespace

int main() {
    plain_resource_or_add_preserves_priority();
    explicit_priority_can_update_existing_resource();
    orthographic_camera_pan_zoom_and_reset();
    return 0;
}
