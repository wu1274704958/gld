#include <aoe2x/Aoe2xFormation.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace gld::ecs::aoe2x {
namespace {
constexpr float Epsilon = 1e-5f;

bool finite(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

glm::vec2 clamp_length(glm::vec2 value, float maximum) {
    const float length = glm::length(value);
    return length > maximum && length > Epsilon
        ? value * (maximum / length) : value;
}

glm::vec2 rotate_between(
    glm::vec2 value, glm::vec2 original, glm::vec2 current) {
    const float cosine = std::clamp(glm::dot(original, current), -1.f, 1.f);
    const float sine = original.x * current.y - original.y * current.x;
    return {value.x * cosine - value.y * sine,
            value.x * sine + value.y * cosine};
}

glm::vec2 direction_or(
    glm::vec2 velocity, glm::vec2 fallback, float minimum_speed) {
    const float squared_length = glm::dot(velocity, velocity);
    return squared_length > minimum_speed * minimum_speed
        ? velocity / std::sqrt(squared_length) : fallback;
}

glm::vec2 rotate_towards(
    glm::vec2 current, glm::vec2 target, float maximum_angle) {
    if (!(maximum_angle > 0.f)) return current;
    const float cosine = std::clamp(glm::dot(current, target), -1.f, 1.f);
    const float sine = current.x * target.y - current.y * target.x;
    const float angle = std::atan2(sine, cosine);
    if (std::abs(angle) <= maximum_angle) return target;
    const float step = std::copysign(maximum_angle, angle);
    const float c = std::cos(step);
    const float s = std::sin(step);
    return {current.x * c - current.y * s,
            current.x * s + current.y * c};
}

float signed_angle(glm::vec2 from, glm::vec2 to) {
    const float cosine = std::clamp(glm::dot(from, to), -1.f, 1.f);
    const float sine = from.x * to.y - from.y * to.x;
    return std::atan2(sine, cosine);
}

bool valid_layout(const FormationLayout& layout, std::uint32_t count,
                  float cell_size) {
    if (layout.relative_positions.size() != count ||
        layout.captain_index >= layout.relative_positions.size()) return false;
    std::vector<glm::vec2> ordered;
    ordered.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& point = layout.relative_positions[
            (layout.captain_index + i) % count];
        if (!finite(point)) return false;
        for (const auto previous : ordered)
            if (glm::length(point - previous) <= Epsilon) return false;
        ordered.push_back(point);
    }
    const float maximum_gap = cell_size * 1.4142137f + Epsilon;
    for (std::size_t i = 1; i < ordered.size(); ++i) {
        const float gap = glm::length(ordered[i] - ordered[i - 1u]);
        if (!(gap > Epsilon) || gap > maximum_gap) return false;
    }
    return true;
}

void stop_unit(entt::registry& reg, entt::entity entity) {
    if (auto* locomotion = reg.try_get<aoe::AoeLocomotionState>(entity)) {
        locomotion->previous_velocity = locomotion->velocity;
        locomotion->velocity = {0.f, 0.f};
        locomotion->actual_speed = 0.f;
        locomotion->effective_max_speed = 0.f;
    }
}

void fail_command(entt::registry& reg, entt::entity captain,
                  FormationAttackMove& order) {
    order.status = FormationAttackMoveStatus::Failed;
    if (reg.valid(captain)) {
        reg.remove<Aoe2xNavigationDestination, Aoe2xRoutePlan,
                   FormationMotionState>(captain);
        stop_unit(reg, captain);
    }
}

bool valid_spawn_options(const FormationSpawnOptions& options) {
    return options.count && finite(options.center) &&
        std::isfinite(options.spacing) && options.spacing >= 0.f &&
        std::isfinite(options.unit_radius) && options.unit_radius > 0.f &&
        std::isfinite(options.movement_speed) && options.movement_speed > 0.f &&
        finite(options.forward) &&
        glm::dot(options.forward, options.forward) > Epsilon * Epsilon;
}

std::vector<glm::vec2> world_offsets(
    const FormationLayout& layout, std::uint32_t count, glm::vec2 forward) {
    std::vector<glm::vec2> result;
    result.reserve(count);
    const glm::vec2 right{forward.y, -forward.x};
    for (std::size_t i = 0; i < count; ++i) {
        const auto local = layout.relative_positions[
            (layout.captain_index + i) % count];
        result.push_back(right * local.x + forward * local.y);
    }
    return result;
}

entt::entity spawn_formation_unit(EcsWorld& world,
    const FormationSpawnOptions& options, glm::vec2 position,
    glm::vec2 forward) {
    auto& reg = world.reg();
    const auto unit = world.spawn();
    reg.emplace<aoe::AoePosition>(unit, aoe::AoePosition{position});
    reg.emplace<aoe::AoePositionHistory>(
        unit, aoe::AoePositionHistory{position});
    reg.emplace<aoe::AoeCollider>(unit, aoe::AoeCollider{
        options.unit_radius, options.unit_radius,
        options.unit_radius * 2.f});
    reg.emplace<aoe::AoeMovement>(
        unit, aoe::AoeMovement{options.movement_speed});
    reg.emplace<aoe::AoeLocomotionState>(unit);
    reg.emplace<aoe::AoeDirection>(unit, aoe::AoeDirection{forward});
    reg.emplace<UnitTargetPosition>(unit, UnitTargetPosition{position});
    reg.emplace<UnitFormationDirection>(
        unit, UnitFormationDirection{forward});
    reg.emplace<UnitFormationMotionState>(unit,
        UnitFormationMotionState{
            UnitFormationMotionPhase::AligningWithCaptain, forward});
    return unit;
}

void connect_follow_chain(
    entt::registry& reg, entt::entity squad, const SquadInfo& info) {
    // The vector is captured once in spawn orientation. Runtime following
    // rotates this immutable vector by the predecessor's direction change.
    for (std::size_t i = 0; i < info.units.size(); ++i) {
        const auto unit = info.units[i];
        const auto followed = i ? info.units[i - 1u] : entt::null;
        glm::vec2 relative{0.f};
        if (followed != entt::null)
            relative = reg.get<aoe::AoePosition>(followed).value -
                       reg.get<aoe::AoePosition>(unit).value;
        reg.emplace<UnitSquadInfo>(
            unit, UnitSquadInfo{squad, followed, relative});
    }
}

void fail_spawn(entt::registry& reg, entt::entity squad,
                FormationSpawnState& state) {
    state.status = FormationSpawnStatus::Failed;
    reg.remove<FormationSpawnRequest>(squad);
}

void spawn_pending_formation(EcsWorld& world, entt::entity squad,
    const FormationSpawnOptions& options, glm::vec2 center,
    const FormationRegistry* registry, FormationSpawnState& state) {
    auto& reg = world.reg();
    const float cell_size = options.unit_radius * 2.f + options.spacing;
    const auto layout = registry
        ? registry->generate(options.formation,
            FormationGenerateContext{options.count, cell_size})
        : FormationLayout{};
    if (!valid_layout(layout, options.count, cell_size)) {
        fail_spawn(reg, squad, state);
        return;
    }

    const glm::vec2 forward = glm::normalize(options.forward);
    const auto offsets = world_offsets(layout, options.count, forward);
    SquadInfo info;
    info.formation = options.formation;
    info.units.reserve(options.count);
    for (const auto offset : offsets)
        info.units.push_back(spawn_formation_unit(
            world, options, center + offset, forward));

    info.captain = info.units.front();
    connect_follow_chain(reg, squad, info);
    reg.emplace<SquadCaptainInfo>(info.captain, SquadCaptainInfo{squad});
    reg.emplace<SquadInfo>(squad, std::move(info));
    reg.emplace<FormationAttackMove>(squad);
    state.status = FormationSpawnStatus::Ready;
    reg.remove<FormationSpawnRequest>(squad);
}

bool command_squad_valid(const entt::registry& reg, entt::entity squad) {
    return reg.valid(squad) &&
        reg.all_of<SquadInfo, FormationSpawnState>(squad) &&
        reg.get<FormationSpawnState>(squad).status ==
            FormationSpawnStatus::Ready;
}

bool command_captain_valid(
    const entt::registry& reg, entt::entity captain) {
    return reg.valid(captain) &&
        reg.all_of<aoe::AoePosition, aoe::AoeCollider,
                   SquadCaptainInfo>(captain);
}

void begin_attack_move(entt::registry& reg, entt::entity squad,
                       entt::entity captain, glm::vec2 destination) {
    auto& order = reg.get_or_emplace<FormationAttackMove>(squad);
    order.destination = destination;
    order.status = FormationAttackMoveStatus::Running;
    ++order.revision;
    reg.emplace_or_replace<Aoe2xNavigationDestination>(
        captain, Aoe2xNavigationDestination{destination});
    reg.emplace_or_replace<Aoe2xRoutePlan>(captain, Aoe2xRoutePlan{});
    reg.emplace_or_replace<FormationMotionState>(captain,
        FormationMotionState{0,
            reg.get<aoe::AoePosition>(captain).value, order.revision});
}

struct MotionParameters {
    float dt = 0.f;
    float acceleration = 0.f;
    float direction_speed = Epsilon;
    float turn_speed = 0.f;
    float follower_response = 0.f;
    FormationSettings settings;
    const aoe::AoeLogicMap* map = nullptr;
};

struct PlannedMotion {
    entt::entity entity{entt::null};
    glm::vec2 velocity{0.f};
    glm::vec2 target{0.f};
    glm::vec2 direction{1.f, 0.f};
    float speed_limit = 0.f;
    bool captain = false;
};

struct TurnFirstMotion {
    glm::vec2 velocity{0.f};
    glm::vec2 direction{1.f, 0.f};
};

bool direction_aligned(glm::vec2 current, glm::vec2 target) {
    return glm::dot(current, target) >= 1.f - Epsilon;
}

TurnFirstMotion turn_in_place(glm::vec2 current_direction,
    glm::vec2 desired_direction, const MotionParameters& parameters) {
    return {{0.f, 0.f}, rotate_towards(current_direction,
        desired_direction, parameters.turn_speed * parameters.dt)};
}

TurnFirstMotion move_forward(glm::vec2 current_direction,
    glm::vec2 current_velocity, glm::vec2 desired_velocity,
    const MotionParameters& parameters) {
    const float desired_speed = glm::length(desired_velocity);
    const float movement_speed = desired_speed > parameters.direction_speed
        ? desired_speed : 0.f;
    const glm::vec2 travel_direction = direction_or(
        desired_velocity, current_direction, parameters.direction_speed);
    const glm::vec2 direction = rotate_towards(current_direction,
        travel_direction, parameters.turn_speed * parameters.dt);

    // Acceleration changes only the forward scalar speed. Locomotion can
    // therefore never acquire a sideways or backward component.
    const float current_forward_speed =
        std::max(0.f, glm::dot(current_velocity, direction));
    const float maximum_delta = parameters.acceleration * parameters.dt;
    const float speed_delta = std::clamp(
        movement_speed - current_forward_speed, -maximum_delta, maximum_delta);
    return {direction * std::max(0.f, current_forward_speed + speed_delta),
            direction};
}

TurnFirstMotion plan_turn_move_state(UnitFormationMotionState& state,
    glm::vec2 current_direction, glm::vec2 current_velocity,
    glm::vec2 desired_velocity, const MotionParameters& parameters) {
    const glm::vec2 travel_direction = direction_or(
        desired_velocity, current_direction, parameters.direction_speed);

    if (state.phase != UnitFormationMotionPhase::TurningToSlot &&
        state.phase != UnitFormationMotionPhase::MovingToSlot) {
        state.phase = UnitFormationMotionPhase::TurningToSlot;
        state.locked_move_direction = travel_direction;
    }

    if (state.phase == UnitFormationMotionPhase::TurningToSlot) {
        const auto motion = turn_in_place(current_direction,
            state.locked_move_direction, parameters);
        if (direction_aligned(
                motion.direction, state.locked_move_direction))
            state.phase = UnitFormationMotionPhase::MovingToSlot;
        return motion;
    }

    const float reorient_angle = std::max(
        0.f, parameters.settings.movement_reorient_radians);
    if (std::abs(signed_angle(current_direction, travel_direction)) >
        reorient_angle) {
        state.phase = UnitFormationMotionPhase::TurningToSlot;
        state.locked_move_direction = travel_direction;
        return turn_in_place(
            current_direction, state.locked_move_direction, parameters);
    }
    return move_forward(current_direction, current_velocity,
        desired_velocity, parameters);
}

bool formation_members_valid(
    const entt::registry& reg, const SquadInfo& info) {
    if (info.units.empty() || !reg.valid(info.captain)) return false;
    for (const auto unit : info.units)
        if (!reg.valid(unit) ||
            !reg.all_of<aoe::AoePosition, aoe::AoeMovement,
                        aoe::AoeLocomotionState, UnitSquadInfo,
                        UnitTargetPosition, UnitFormationDirection,
                        UnitFormationMotionState, aoe::AoeDirection>(unit))
            return false;
    return true;
}

void finish_attack_move(entt::registry& reg, entt::entity captain,
                        FormationAttackMove& order) {
    order.status = FormationAttackMoveStatus::Completed;
    reg.remove<Aoe2xNavigationDestination, Aoe2xRoutePlan,
               FormationMotionState>(captain);
}

void advance_route_cursor(FormationMotionState& motion,
    const Aoe2xRoutePlan& route, glm::vec2 position,
    const FormationSettings& settings) {
    // A waypoint is consumed either when reached or when movement has already
    // crossed its segment plane. This avoids steering back to stale points.
    while (motion.waypoint_index < route.waypoints.size()) {
        const auto index = motion.waypoint_index;
        const glm::vec2 previous = index
            ? route.waypoints[index - 1u] : motion.route_start;
        const glm::vec2 segment = route.waypoints[index] - previous;
        const bool reached = glm::length(
            route.waypoints[index] - position) <= settings.waypoint_radius;
        const bool passed = glm::dot(
            position - route.waypoints[index], segment) >= 0.f;
        if (!reached && !passed) break;
        ++motion.waypoint_index;
    }
}

glm::vec2 path_lookahead_target(const FormationMotionState& motion,
    const Aoe2xRoutePlan& route, glm::vec2 position, glm::vec2 destination,
    float path_lookahead) {
    if (motion.waypoint_index >= route.waypoints.size()) return destination;
    float lookahead = std::max(0.f, path_lookahead);
    glm::vec2 cursor = position;
    glm::vec2 target = route.waypoints[motion.waypoint_index];
    for (std::size_t i = motion.waypoint_index;
         i < route.waypoints.size(); ++i) {
        const glm::vec2 delta = route.waypoints[i] - cursor;
        const float length = glm::length(delta);
        if (length > lookahead && length > Epsilon)
            return cursor + delta * (lookahead / length);
        target = route.waypoints[i];
        lookahead -= length;
        cursor = route.waypoints[i];
        if (!(lookahead > 0.f)) break;
    }
    return target;
}

float remaining_route_distance(const FormationMotionState& motion,
    const Aoe2xRoutePlan& route, glm::vec2 position, glm::vec2 target) {
    float remaining = glm::length(target - position);
    glm::vec2 cursor = target;
    for (std::size_t i = motion.waypoint_index;
         i < route.waypoints.size(); ++i) {
        remaining += glm::length(route.waypoints[i] - cursor);
        cursor = route.waypoints[i];
    }
    return remaining;
}

glm::vec2 captain_route_desired_velocity(entt::registry& reg,
    entt::entity captain,
    FormationAttackMove& order, const Aoe2xRoutePlan& route,
    glm::vec2 position, const MotionParameters& parameters,
    glm::vec2& target) {
    reg.remove<Aoe2xNavigationDestination>(captain);
    auto& motion = reg.get_or_emplace<FormationMotionState>(captain);
    if (motion.command_revision != order.revision)
        motion = {0, position, order.revision};
    advance_route_cursor(motion, route, position, parameters.settings);
    target = path_lookahead_target(motion, route, position,
        order.destination, parameters.settings.path_lookahead);

    // Brake against the remaining polyline distance, not only the current
    // lookahead target, so corners remain smooth while the endpoint is exact.
    const float remaining = remaining_route_distance(
        motion, route, position, target);
    const glm::vec2 delta = target - position;
    const float distance = glm::length(delta);
    const float maximum_speed = reg.get<aoe::AoeMovement>(captain).speed;
    const float braking_speed = parameters.acceleration > Epsilon
        ? std::sqrt(2.f * parameters.acceleration * std::max(0.f, remaining))
        : maximum_speed;
    const float speed = std::min(maximum_speed, braking_speed);
    return distance > Epsilon
        ? delta * (speed / distance) : glm::vec2(0.f);
}

PlannedMotion plan_captain_motion(entt::registry& reg, entt::entity captain,
    FormationAttackMove& order, const MotionParameters& parameters) {
    auto& position = reg.get<aoe::AoePosition>(captain).value;
    const auto& locomotion = reg.get<aoe::AoeLocomotionState>(captain);
    glm::vec2 target = position;
    glm::vec2 desired_velocity{0.f};

    if (order.status == FormationAttackMoveStatus::Running) {
        if (glm::length(order.destination - position) <=
            parameters.settings.arrival_radius) {
            position = order.destination;
            target = order.destination;
            finish_attack_move(reg, captain, order);
        } else if (const auto* route = reg.try_get<Aoe2xRoutePlan>(captain)) {
            if (route->status == Aoe2xRouteStatus::NoPath ||
                route->status == Aoe2xRouteStatus::Invalid)
                fail_command(reg, captain, order);
            else if (route->status == Aoe2xRouteStatus::Ready)
                desired_velocity = captain_route_desired_velocity(
                    reg, captain, order, *route, position, parameters, target);
        }
    }

    const auto& stored_direction =
        reg.get<aoe::AoeDirection>(captain).value;
    auto& movement_state =
        reg.get<UnitFormationMotionState>(captain);
    TurnFirstMotion motion{{0.f, 0.f}, stored_direction};
    if (order.status == FormationAttackMoveStatus::Running &&
        glm::length(desired_velocity) > parameters.direction_speed) {
        motion = plan_turn_move_state(movement_state, stored_direction,
            locomotion.velocity, desired_velocity, parameters);
    } else {
        movement_state.phase =
            UnitFormationMotionPhase::AligningWithCaptain;
        movement_state.locked_move_direction = stored_direction;
    }
    return {captain, motion.velocity, target, motion.direction,
        reg.get<aoe::AoeMovement>(captain).speed, true};
}

bool append_follower_motions(entt::registry& reg, const SquadInfo& info,
    const MotionParameters& parameters, std::vector<PlannedMotion>& motions) {
    const auto& captain_direction =
        reg.get<UnitFormationDirection>(info.captain);
    const glm::vec2 current_captain_direction =
        reg.get<aoe::AoeDirection>(info.captain).value;
    const glm::vec2 expected_direction = motions.front().direction;
    const glm::vec2 captain_position =
        reg.get<aoe::AoePosition>(info.captain).value;
    const glm::vec2 captain_velocity = motions.front().velocity;
    const float angular_velocity = parameters.dt > Epsilon
        ? std::clamp(signed_angle(current_captain_direction,
                         expected_direction) / parameters.dt,
              -parameters.turn_speed, parameters.turn_speed)
        : 0.f;
    glm::vec2 expected_predecessor_position = captain_position;
    for (std::size_t i = 1; i < info.units.size(); ++i) {
        const auto unit = info.units[i];
        const auto& member = reg.get<UnitSquadInfo>(unit);
        if (member.followed != info.units[i - 1u]) return false;

        // Every link uses the same captain rotation. Intermediate member
        // facing never changes formation geometry, so targets are deterministic.
        const glm::vec2 rotated_relative = rotate_between(
            member.followed_relative_to_self,
            captain_direction.original, expected_direction);
        // Build the entire ideal chain from the captain. The target does not
        // inherit the predecessor's physical error, so lag cannot propagate
        // and accumulate toward the tail of the formation.
        const glm::vec2 target =
            expected_predecessor_position - rotated_relative;
        expected_predecessor_position = target;
        const glm::vec2 position = reg.get<aoe::AoePosition>(unit).value;
        const glm::vec2 captain_relative = target - captain_position;
        const glm::vec2 rotational_velocity = angular_velocity *
            glm::vec2{-captain_relative.y, captain_relative.x};
        glm::vec2 desired = captain_velocity + rotational_velocity +
            (target - position) / parameters.follower_response;
        const float maximum_speed = reg.get<aoe::AoeMovement>(unit).speed *
            std::max(1.f,
                parameters.settings.follower_catchup_speed_multiplier);
        desired = clamp_length(desired, maximum_speed);
        const glm::vec2 stored_direction =
            reg.get<aoe::AoeDirection>(unit).value;
        const auto& locomotion =
            reg.get<aoe::AoeLocomotionState>(unit);
        auto& movement_state =
            reg.get<UnitFormationMotionState>(unit);
        const float slot_distance = glm::length(target - position);
        const float slot_exit_radius = std::max(
            parameters.settings.arrival_radius,
            parameters.settings.slot_exit_radius);
        TurnFirstMotion motion{{0.f, 0.f}, stored_direction};

        if (movement_state.phase ==
            UnitFormationMotionPhase::AligningWithCaptain) {
            if (slot_distance > slot_exit_radius) {
                movement_state.phase =
                    UnitFormationMotionPhase::TurningToSlot;
                const glm::vec2 slot_direction = direction_or(
                    target - position, stored_direction, Epsilon);
                movement_state.locked_move_direction = direction_or(
                    desired, slot_direction, parameters.direction_speed);
                motion = turn_in_place(stored_direction,
                    movement_state.locked_move_direction, parameters);
            } else {
                // Slot alignment is latched until the wider exit radius is
                // crossed. Begin from the unit's current persistent facing.
                motion = turn_in_place(
                    stored_direction, expected_direction, parameters);
                const glm::vec2 travel_direction = direction_or(
                    desired, motion.direction, parameters.direction_speed);
                const bool transport_is_forward =
                    glm::length(desired) <= parameters.direction_speed ||
                    std::abs(signed_angle(
                        motion.direction, travel_direction)) <=
                        std::max(0.f,
                            parameters.settings.movement_reorient_radians);
                if (direction_aligned(
                        motion.direction, expected_direction) &&
                    transport_is_forward)
                    motion = move_forward(motion.direction,
                        locomotion.velocity, desired, parameters);
            }
        } else if (slot_distance <=
            parameters.settings.arrival_radius) {
            movement_state.phase =
                UnitFormationMotionPhase::AligningWithCaptain;
            movement_state.locked_move_direction = expected_direction;
            motion = turn_in_place(
                stored_direction, expected_direction, parameters);
        } else {
            motion = plan_turn_move_state(movement_state,
                stored_direction, locomotion.velocity, desired, parameters);
        }
        motions.push_back(
            {unit, motion.velocity, target, motion.direction,
                maximum_speed, false});
    }
    return true;
}

void integrate_motion(entt::registry& reg, const PlannedMotion& motion,
    const MotionParameters& parameters) {
    auto& position = reg.get<aoe::AoePosition>(motion.entity);
    auto& locomotion = reg.get<aoe::AoeLocomotionState>(motion.entity);
    if (auto* history = reg.try_get<aoe::AoePositionHistory>(motion.entity))
        history->previous = position.value;
    locomotion.previous_velocity = locomotion.velocity;

    glm::vec2 displacement = motion.velocity * parameters.dt;
    if (motion.captain && parameters.map && parameters.map->valid()) {
        const auto& collider = reg.get<aoe::AoeCollider>(motion.entity);
        displacement *= parameters.map->static_safe_fraction(
            position.value, position.value + displacement,
            {collider.radius_x, collider.radius_y});
    }
    position.value += displacement;
    locomotion.velocity = displacement / parameters.dt;
    locomotion.actual_speed = glm::length(locomotion.velocity);
    locomotion.effective_max_speed = motion.speed_limit;
    locomotion.distance_travelled += glm::length(displacement);
    reg.get<aoe::AoeDirection>(motion.entity).value = motion.direction;
    reg.get<UnitTargetPosition>(motion.entity).value = motion.target;
}

void finish_after_integration(entt::registry& reg, entt::entity captain,
                              FormationAttackMove& order,
                              const FormationSettings& settings) {
    if (order.status != FormationAttackMoveStatus::Running ||
        glm::length(order.destination -
            reg.get<aoe::AoePosition>(captain).value) > settings.arrival_radius)
        return;
    reg.get<aoe::AoePosition>(captain).value = order.destination;
    stop_unit(reg, captain);
    finish_attack_move(reg, captain, order);
}
} // namespace

FormationLayout CompactSquareFormation::generate(
    const FormationGenerateContext& context) {
    if (!context.count || context.count > 1024u ||
        !std::isfinite(context.cell_size) || !(context.cell_size > 0.f))
        return {};
    std::uint32_t columns = static_cast<std::uint32_t>(
        std::ceil(std::sqrt(static_cast<double>(context.count))));
    if (!(columns & 1u)) ++columns;
    std::vector<glm::ivec2> cells;
    cells.reserve(context.count);
    std::uint32_t remaining = context.count;
    for (int row = 0; remaining; ++row) {
        const auto full_row_count = std::min(
            columns, static_cast<std::uint32_t>(row * 2 + 1));
        const auto row_count = std::min(full_row_count, remaining);
        const int half = static_cast<int>(full_row_count / 2u);
        const bool left_to_right = (row & 1) != 0;
        for (std::uint32_t column = 0; column < row_count; ++column) {
            const int x = left_to_right
                ? -half + static_cast<int>(column)
                : half - static_cast<int>(column);
            cells.push_back({x, -row});
        }
        remaining -= row_count;
    }
    FormationLayout result;
    result.relative_positions.reserve(context.count);
    for (const auto cell : cells)
        result.relative_positions.push_back(
            glm::vec2(cell) * context.cell_size);
    result.captain_index = 0;
    return result;
}

void FormationRegistry::bind_erased(FormationType type, GenerateFn function) {
    if (!function) throw std::invalid_argument("formation generator is null");
    if (!entries_.emplace(type, function).second)
        throw std::invalid_argument("duplicate formation generator");
}

bool FormationRegistry::contains(FormationType type) const {
    return entries_.contains(type);
}

FormationLayout FormationRegistry::generate(
    FormationType type, const FormationGenerateContext& context) const {
    const auto it = entries_.find(type);
    return it == entries_.end() ? FormationLayout{} : it->second(context);
}

entt::entity spawn_aoe2x_formation(
    EcsWorld& world, const FormationSpawnOptions& options) {
    if (!valid_spawn_options(options)) return entt::null;
    const auto squad = world.spawn();
    world.reg().emplace<aoe::AoePosition>(squad, aoe::AoePosition{options.center});
    world.reg().emplace<FormationSpawnRequest>(
        squad, FormationSpawnRequest{options});
    world.reg().emplace<FormationSpawnState>(squad);
    return squad;
}

bool request_aoe2x_formation_attack_move(
    EcsWorld& world, entt::entity squad, glm::vec2 destination) {
    if (!world.reg().valid(squad) || !finite(destination) ||
        !world.reg().all_of<SquadInfo, FormationSpawnState>(squad) ||
        world.reg().get<FormationSpawnState>(squad).status !=
            FormationSpawnStatus::Ready)
        return false;
    world.resource_or_add<FormationCommands>().queue.push_back(
        FormationAttackMoveCommand{squad, destination});
    return true;
}

void SpawnFormationSystem::run(EcsWorld& world, std::uint64_t) {
    auto& reg = world.reg();
    const auto* registry = world.try_resource<FormationRegistry>();
    const auto view = reg.view<const FormationSpawnRequest, FormationSpawnState,
                               const aoe::AoePosition>();
    for (const auto squad : view) {
        auto& state = view.get<FormationSpawnState>(squad);
        if (state.status != FormationSpawnStatus::Pending) continue;
        const auto options = view.get<const FormationSpawnRequest>(squad).options;
        const glm::vec2 center = view.get<const aoe::AoePosition>(squad).value;
        spawn_pending_formation(
            world, squad, options, center, registry, state);
    }
}

void FormationCommandSystem::run(EcsWorld& world, std::uint64_t) {
    auto& reg = world.reg();
    auto& commands = world.resource_or_add<FormationCommands>().queue;
    auto pending = std::move(commands);
    commands.clear();
    for (const auto& command : pending) {
        if (!command_squad_valid(reg, command.squad)) continue;
        const auto captain = reg.get<SquadInfo>(command.squad).captain;
        if (!command_captain_valid(reg, captain)) continue;
        begin_attack_move(
            reg, command.squad, captain, command.destination);
    }
}

void FormationSystem::run(EcsWorld& world, std::uint64_t) {
    auto& reg = world.reg();
    const auto& gameplay = world.resource_or_add<aoe::AoeGameplaySettings>();
    const auto& navigation = world.resource_or_add<aoe::AoeNavigationSettings>();
    const auto settings = world.resource_or_add<FormationSettings>();
    const float dt = static_cast<float>(gameplay.fixed_dt);
    if (!(dt > 0.f) || !std::isfinite(dt)) return;
    const MotionParameters parameters{
        dt,
        std::max(0.f, navigation.steering_max_acceleration),
        std::max(Epsilon, navigation.steering_stalled_speed),
        std::max(0.f,
            navigation.steering_max_turn_radians_per_second),
        std::max(settings.follower_response_seconds, dt),
        settings,
        world.try_resource<aoe::AoeLogicMap>()};

    for (const auto squad : reg.view<const SquadInfo, FormationSpawnState,
                                      FormationAttackMove>()) {
        if (reg.get<FormationSpawnState>(squad).status !=
            FormationSpawnStatus::Ready)
            continue;
        const auto& info = reg.get<const SquadInfo>(squad);
        auto& order = reg.get<FormationAttackMove>(squad);
        if (!formation_members_valid(reg, info)) {
            fail_command(reg, info.captain, order);
            continue;
        }

        std::vector<PlannedMotion> motions;
        motions.reserve(info.units.size());
        const auto captain = info.captain;
        motions.push_back(
            plan_captain_motion(reg, captain, order, parameters));
        if (!append_follower_motions(
                reg, info, parameters, motions)) {
            fail_command(reg, captain, order);
            continue;
        }
        for (const auto& motion : motions)
            integrate_motion(reg, motion, parameters);
        finish_after_integration(reg, captain, order, settings);
    }
}

} // namespace gld::ecs::aoe2x
