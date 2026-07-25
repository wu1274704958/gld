#include <cstdlib>
#include <vector>

#include <ecs/EcsWorld.hpp>

using namespace gld::ecs;

namespace {

void require(bool condition) {
    if (!condition)
        std::abort();
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

} // namespace

int main() {
    plain_resource_or_add_preserves_priority();
    explicit_priority_can_update_existing_resource();
    return 0;
}
