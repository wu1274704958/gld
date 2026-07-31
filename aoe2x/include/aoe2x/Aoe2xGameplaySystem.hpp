#pragma once

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <entt/entt.hpp>
#include <ecs/App.hpp>

namespace gld::ecs::aoe2x {

template<class... Components>
struct Aoe2xComponentList {};

namespace detail {
template<class T>
struct component_list_traits { static constexpr bool valid = false; };

template<class... Components>
struct component_list_traits<Aoe2xComponentList<Components...>> {
    static constexpr bool valid = true;
};

template<class... Components>
struct component_list_is_unique_impl : std::true_type {};

template<class First, class... Rest>
struct component_list_is_unique_impl<First, Rest...>
    : std::bool_constant<((!std::is_same_v<First, Rest>) && ...) &&
                         component_list_is_unique_impl<Rest...>::value> {};

template<class... Components>
consteval bool component_list_is_unique(Aoe2xComponentList<Components...>) {
    return component_list_is_unique_impl<Components...>::value;
}

template<class Component, class List>
struct component_list_excludes;

template<class Component, class... Components>
struct component_list_excludes<Component, Aoe2xComponentList<Components...>>
    : std::bool_constant<(!std::is_same_v<Component, Components> && ...)> {};

template<class Left, class Right>
struct component_lists_disjoint_impl;

template<class... Left, class Right>
struct component_lists_disjoint_impl<Aoe2xComponentList<Left...>, Right>
    : std::bool_constant<(component_list_excludes<Left, Right>::value && ...)> {};

template<class Left, class Right>
consteval bool component_lists_disjoint(Left, Right) {
    return component_lists_disjoint_impl<Left, Right>::value;
}

template<class List>
struct ComponentListIds;

template<class... Components>
struct ComponentListIds<Aoe2xComponentList<Components...>> {
    static std::vector<entt::id_type> make() {
        return {entt::type_hash<Components>::value()...};
    }
};
} // namespace detail

enum class Aoe2xGameplayPhase : std::uint8_t {
    Prepare,
    Command,
    Navigation,
    MovementIntent,
    LocalAvoidance,
    GlobalMotion,
    Movement,
    Combat,
    Lifecycle
};

struct Aoe2xGameplaySystemTiming {
    std::uint64_t samples = 0;
    std::optional<double> average_ms;
    std::optional<double> peak_ms;

    void record_sample(double elapsed_ms) {
        ++samples;
        total_ms_ += elapsed_ms;
        average_ms = total_ms_ / static_cast<double>(samples);
        peak_ms = std::max(peak_ms.value_or(0.0), elapsed_ms);
    }

private:
    double total_ms_ = 0.0;
    friend class Aoe2xGameplaySystemRegistry;
};

struct Aoe2xGameplaySystemDescriptor {
    std::string name;
    Stage app_stage = Stage::PreUpdate;
    Aoe2xGameplayPhase phase = Aoe2xGameplayPhase::Prepare;
    std::vector<entt::id_type> read_only_components;
    std::vector<entt::id_type> write_only_components;
    std::vector<entt::id_type> read_write_components;
    Aoe2xGameplaySystemTiming timing;
};

template<class T>
concept Aoe2xGameplaySystem = requires(EcsWorld& world, std::uint64_t tick) {
    typename T::ReadOnlyComponents;
    typename T::WriteOnlyComponents;
    typename T::ReadWriteComponents;
    { T::name } -> std::convertible_to<std::string_view>;
    { T::app_stage } -> std::convertible_to<Stage>;
    { T::phase } -> std::convertible_to<Aoe2xGameplayPhase>;
    { T::run(world, tick) } -> std::same_as<void>;
} && detail::component_list_traits<typename T::ReadOnlyComponents>::valid &&
     detail::component_list_traits<typename T::WriteOnlyComponents>::valid &&
     detail::component_list_traits<typename T::ReadWriteComponents>::valid &&
     detail::component_list_is_unique(typename T::ReadOnlyComponents{}) &&
     detail::component_list_is_unique(typename T::WriteOnlyComponents{}) &&
     detail::component_list_is_unique(typename T::ReadWriteComponents{}) &&
     detail::component_lists_disjoint(
         typename T::ReadOnlyComponents{}, typename T::WriteOnlyComponents{}) &&
     detail::component_lists_disjoint(
         typename T::ReadOnlyComponents{}, typename T::ReadWriteComponents{}) &&
     detail::component_lists_disjoint(
         typename T::WriteOnlyComponents{}, typename T::ReadWriteComponents{});

class Aoe2xGameplaySystemRegistry {
public:
    template<Aoe2xGameplaySystem System>
    Aoe2xGameplaySystemDescriptor& register_system() {
        const std::string_view system_name = System::name;
        if (system_name.empty())
            throw std::invalid_argument("aoe2x gameplay system requires a name");
        if (find(system_name))
            throw std::invalid_argument("duplicate aoe2x gameplay system name");
        systems_.push_back(Aoe2xGameplaySystemDescriptor{
            .name = std::string(system_name),
            .app_stage = System::app_stage,
            .phase = System::phase,
            .read_only_components = detail::ComponentListIds<
                typename System::ReadOnlyComponents>::make(),
            .write_only_components = detail::ComponentListIds<
                typename System::WriteOnlyComponents>::make(),
            .read_write_components = detail::ComponentListIds<
                typename System::ReadWriteComponents>::make()});
        return systems_.back();
    }

    Aoe2xGameplaySystemDescriptor* find(std::string_view name) {
        for (auto& system : systems_)
            if (system.name == name) return &system;
        return nullptr;
    }
    const Aoe2xGameplaySystemDescriptor* find(std::string_view name) const {
        for (const auto& system : systems_)
            if (system.name == name) return &system;
        return nullptr;
    }
    const std::vector<Aoe2xGameplaySystemDescriptor>& systems() const {
        return systems_;
    }
    void reset_timing() {
        for (auto& system : systems_) {
            system.timing.samples = 0;
            system.timing.average_ms.reset();
            system.timing.peak_ms.reset();
            system.timing.total_ms_ = 0.0;
        }
    }

private:
    std::vector<Aoe2xGameplaySystemDescriptor> systems_;
};

template<Aoe2xGameplaySystem System>
Aoe2xGameplaySystemDescriptor& register_aoe2x_gameplay_system(EcsWorld& world) {
    return world.resource_or_add<Aoe2xGameplaySystemRegistry>()
        .template register_system<System>();
}

template<Aoe2xGameplaySystem System>
void run_aoe2x_gameplay_system(EcsWorld& world, std::uint64_t tick) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const auto started = std::chrono::steady_clock::now();
    System::run(world, tick);
    if (auto* registry = world.try_resource<Aoe2xGameplaySystemRegistry>()) {
        if (auto* descriptor = registry->find(System::name)) {
            const double elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
            descriptor->timing.record_sample(elapsed);
        }
    }
#else
    System::run(world, tick);
#endif
}

} // namespace gld::ecs::aoe2x
