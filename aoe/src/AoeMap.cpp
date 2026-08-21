#include <aoe/AoeMap.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace gld::ecs::aoe {
namespace {
using json = nlohmann::json;
constexpr float Epsilon = 1e-5f;

float finite_number(const json& value, const char* name) {
    const double number = value.get<double>();
    if (!std::isfinite(number) || number < -std::numeric_limits<float>::max() ||
        number > std::numeric_limits<float>::max())
        throw std::runtime_error(std::string(name) + " must be finite");
    return static_cast<float>(number);
}

glm::vec2 vec2_value(const json& value, const char* name) {
    if (!value.is_object() || !value.contains("x") || !value.contains("y"))
        throw std::runtime_error(std::string(name) + " must contain x/y");
    return {finite_number(value.at("x"), name),
            finite_number(value.at("y"), name)};
}

bool valid_obstacle(const AoeStaticObstacleDesc& value) {
    if (!std::isfinite(value.center.x) || !std::isfinite(value.center.y))
        return false;
    if (value.shape == AoeStaticObstacleShape::Aabb)
        return std::isfinite(value.half_extents.x) &&
               std::isfinite(value.half_extents.y) &&
               value.half_extents.x > 0.f && value.half_extents.y > 0.f;
    return std::isfinite(value.radius) && value.radius > 0.f;
}

std::shared_ptr<AoeMapDefinition> parse_map(const json& source) {
    if (source.at("schema_version").get<int>() != 1)
        throw std::runtime_error("unsupported map schema_version");
    if (source.at("kind").get<std::string>() != "aoe_logic_map")
        throw std::runtime_error("map kind must be aoe_logic_map");
    auto result = std::make_shared<AoeMapDefinition>();
    result->id = source.at("id").get<std::string>();
    if (result->id.empty()) throw std::runtime_error("map id must not be empty");
    result->origin = vec2_value(source.at("origin"), "origin");
    result->tile_size = finite_number(source.at("tile_size"), "tile_size");
    if (!(result->tile_size > 0.f))
        throw std::runtime_error("tile_size must be positive");
    result->width = source.at("width").get<std::uint32_t>();
    result->height = source.at("height").get<std::uint32_t>();
    if (!result->width || !result->height)
        throw std::runtime_error("map dimensions must be positive");
    const std::uint64_t vertex_count =
        (static_cast<std::uint64_t>(result->width) + 1) *
        (static_cast<std::uint64_t>(result->height) + 1);
    if (vertex_count > std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("map dimensions overflow");
    const auto& heights = source.at("heights");
    if (!heights.is_array() || heights.size() != vertex_count)
        throw std::runtime_error("heights length must equal (width+1)*(height+1)");
    result->heights.reserve(static_cast<std::size_t>(vertex_count));
    for (const auto& height : heights)
        result->heights.push_back(finite_number(height, "height"));

    std::unordered_set<std::string> ids;
    for (const auto& value : source.value("static_obstacles", json::array())) {
        AoeStaticObstacleDesc obstacle;
        obstacle.source_id = value.at("id").get<std::string>();
        if (obstacle.source_id.empty() || !ids.insert(obstacle.source_id).second)
            throw std::runtime_error("static obstacle ids must be non-empty and unique");
        const auto shape = value.at("shape").get<std::string>();
        obstacle.center = vec2_value(value.at("center"), "obstacle.center");
        if (shape == "aabb") {
            obstacle.shape = AoeStaticObstacleShape::Aabb;
            obstacle.half_extents = vec2_value(
                value.at("half_extents"), "obstacle.half_extents");
        } else if (shape == "circle") {
            obstacle.shape = AoeStaticObstacleShape::Circle;
            obstacle.radius = finite_number(value.at("radius"), "obstacle.radius");
        } else throw std::runtime_error("unsupported static obstacle shape");
        if (!valid_obstacle(obstacle))
            throw std::runtime_error("static obstacle dimensions must be positive and finite");
        result->static_obstacles.push_back(std::move(obstacle));
    }
    return result;
}

float ellipse_segment_enter(glm::vec2 from, glm::vec2 to,
                            glm::vec2 center, glm::vec2 radii) {
    if (!(radii.x > 0.f) || !(radii.y > 0.f)) return 1.f;
    const glm::vec2 start = from - center;
    const glm::vec2 delta = to - from;
    const float a = delta.x * delta.x / (radii.x * radii.x) +
                    delta.y * delta.y / (radii.y * radii.y);
    const float b = 2.f * (start.x * delta.x / (radii.x * radii.x) +
                           start.y * delta.y / (radii.y * radii.y));
    const float c = start.x * start.x / (radii.x * radii.x) +
                    start.y * start.y / (radii.y * radii.y) - 1.f;
    // A unit resting on an obstacle must still be able to move tangentially
    // or away from it; only motion further into the expanded ellipse blocks.
    if (c <= 0.f) return b >= -Epsilon ? 1.f : 0.f;
    // `a` is the squared step length in ellipse space, so an absolute epsilon
    // on it silently ignores every step shorter than roughly radius/300 and
    // lets units walk straight through large obstacles. Both roots share the
    // sign of -b here (c > 0, a >= 0), so a non-negative b means the segment
    // never turns inward and only b < 0 can produce an entry.
    if (b >= 0.f) return 1.f;
    const float discriminant = b * b - 4.f * a * c;
    if (discriminant < 0.f) return 1.f;
    const float root = std::sqrt(std::max(0.f, discriminant));
    // Solving the smaller root as (-b - root) / 2a cancels catastrophically
    // once the step is short relative to the obstacle; this form does not.
    const float enter = 2.f * c / (root - b);
    return enter >= 0.f && enter <= 1.f ? enter : 1.f;
}

float aabb_segment_enter(glm::vec2 from, glm::vec2 to,
                         glm::vec2 center, glm::vec2 half) {
    const glm::vec2 min = center - half;
    const glm::vec2 max = center + half;
    const glm::vec2 delta = to - from;
    const bool inclusive = from.x >= min.x && from.x <= max.x &&
                           from.y >= min.y && from.y <= max.y;
    if (inclusive) {
        const bool strict = from.x > min.x + Epsilon &&
                            from.x < max.x - Epsilon &&
                            from.y > min.y + Epsilon &&
                            from.y < max.y - Epsilon;
        if (strict) return 0.f;
        bool enters_interior = true;
        for (int axis = 0; axis < 2; ++axis) {
            if (from[axis] <= min[axis] + Epsilon)
                enters_interior = enters_interior && delta[axis] > Epsilon;
            else if (from[axis] >= max[axis] - Epsilon)
                enters_interior = enters_interior && delta[axis] < -Epsilon;
        }
        return enters_interior ? 0.f : 1.f;
    }
    float enter = 0.f;
    float leave = 1.f;
    for (int axis = 0; axis < 2; ++axis) {
        if (std::abs(delta[axis]) <= Epsilon) {
            if (from[axis] < min[axis] || from[axis] > max[axis]) return 1.f;
            continue;
        }
        float a = (min[axis] - from[axis]) / delta[axis];
        float b = (max[axis] - from[axis]) / delta[axis];
        if (a > b) std::swap(a, b);
        enter = std::max(enter, a);
        leave = std::min(leave, b);
        if (enter > leave) return 1.f;
    }
    return enter >= 0.f && enter <= 1.f ? enter : 1.f;
}

float before_contact(float value) {
    return value >= 1.f ? 1.f : std::max(0.f, value - 0.0001f);
}
} // namespace

std::shared_ptr<void> AoeMapDefinitionLoader::load_cpu(
    const AoeMapDefinitionDesc& desc, const IFileSystem& fs) {
    try {
        const auto text = fs.read_text(desc.path());
        return text ? parse_map(json::parse(*text)) : nullptr;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[aoe-map] failed to parse %s: %s\n",
                     desc.path().c_str(), error.what());
        return nullptr;
    }
}

std::shared_ptr<AoeMapDefinition> AoeMapDefinitionLoader::finalize(
    std::shared_ptr<void> cpu, const AoeMapDefinitionDesc&) {
    return std::static_pointer_cast<AoeMapDefinition>(std::move(cpu));
}

void AoeLogicMap::visit_static_obstacles(const std::function<void(
    AoeObstacleId, AoeStaticObstacleKind,
    const AoeStaticObstacleDesc&)>& visitor) const {
    if (!visitor) return;
    for (const auto& obstacle : obstacles_)
        visitor(obstacle.id, obstacle.kind, obstacle.desc);
}

AoeLogicMap::AoeLogicMap(const AoeMapDefinition& definition) {
    std::string error;
    if (!reset(definition, &error)) throw std::invalid_argument(error);
}

bool AoeLogicMap::reset(const AoeMapDefinition& definition, std::string* error) {
    const std::uint64_t expected =
        (static_cast<std::uint64_t>(definition.width) + 1) *
        (static_cast<std::uint64_t>(definition.height) + 1);
    if (definition.id.empty() || !definition.width || !definition.height ||
        !std::isfinite(definition.origin.x) || !std::isfinite(definition.origin.y) ||
        !std::isfinite(definition.tile_size) || !(definition.tile_size > 0.f) ||
        expected > std::numeric_limits<std::size_t>::max() ||
        definition.heights.size() != expected ||
        std::any_of(definition.heights.begin(), definition.heights.end(),
                    [](float v) { return !std::isfinite(v); }) ||
        std::any_of(definition.static_obstacles.begin(),
                    definition.static_obstacles.end(),
                    [](const auto& v) { return !valid_obstacle(v); })) {
        if (error) *error = "invalid AoE logic map definition";
        return false;
    }
    id_ = definition.id;
    origin_ = definition.origin;
    tile_size_ = definition.tile_size;
    width_ = definition.width;
    height_ = definition.height;
    heights_ = definition.heights;
    obstacles_.clear();
    base_static_obstacle_count_ = 0;
    obstacle_lookup_.clear();
    next_obstacle_id_ = 1;
    static_buckets_.assign(static_cast<std::size_t>(width_) * height_, {});
    for (const auto& obstacle : definition.static_obstacles) {
        const auto id = next_obstacle_id_++;
        obstacle_lookup_[id] = obstacles_.size();
        obstacles_.push_back({id, AoeStaticObstacleKind::Base, obstacle});
    }
    base_static_obstacle_count_ = obstacles_.size();
    ++static_revision_;
    rebuild_static_index();
    return true;
}

bool AoeLogicMap::contains(glm::vec2 point, glm::vec2 clearance) const {
    if (!valid() || clearance.x < 0.f || clearance.y < 0.f) return false;
    const glm::vec2 min = origin_ + clearance;
    const glm::vec2 max = origin_ + glm::vec2(width_ * tile_size_,
                                              height_ * tile_size_) - clearance;
    return point.x >= min.x && point.y >= min.y &&
           point.x < max.x && point.y < max.y;
}

std::optional<AoeMapCell> AoeLogicMap::world_to_cell(glm::vec2 point) const {
    if (!contains(point)) return std::nullopt;
    const glm::vec2 local = (point - origin_) / tile_size_;
    return AoeMapCell{static_cast<int>(std::floor(local.x)),
                      static_cast<int>(std::floor(local.y))};
}

glm::vec2 AoeLogicMap::cell_center(int x, int y) const {
    return origin_ + (glm::vec2(x, y) + .5f) * tile_size_;
}

std::optional<float> AoeLogicMap::sample_height(glm::vec2 point) const {
    if (!contains(point)) return std::nullopt;
    const glm::vec2 local = (point - origin_) / tile_size_;
    const auto x = static_cast<std::uint32_t>(std::floor(local.x));
    const auto y = static_cast<std::uint32_t>(std::floor(local.y));
    const float tx = local.x - static_cast<float>(x);
    const float ty = local.y - static_cast<float>(y);
    const std::size_t stride = static_cast<std::size_t>(width_) + 1;
    const std::size_t base = static_cast<std::size_t>(y) * stride + x;
    const float low = heights_[base] + (heights_[base + 1] - heights_[base]) * tx;
    const float high = heights_[base + stride] +
        (heights_[base + stride + 1] - heights_[base + stride]) * tx;
    return low + (high - low) * ty;
}

AoeObstacleId AoeLogicMap::add_runtime_static_obstacle(
    const AoeStaticObstacleDesc& obstacle) {
    if (!valid_obstacle(obstacle)) return 0;
    const auto id = next_obstacle_id_++;
    obstacle_lookup_[id] = obstacles_.size();
    obstacles_.push_back({id, AoeStaticObstacleKind::Runtime, obstacle});
    ++static_revision_;
    rebuild_static_index();
    return id;
}

bool AoeLogicMap::update_runtime_static_obstacle(
    AoeObstacleId id, const AoeStaticObstacleDesc& obstacle) {
    const auto it = obstacle_lookup_.find(id);
    if (it == obstacle_lookup_.end() ||
        obstacles_[it->second].kind != AoeStaticObstacleKind::Runtime ||
        !valid_obstacle(obstacle)) return false;
    obstacles_[it->second].desc = obstacle;
    ++static_revision_;
    rebuild_static_index();
    return true;
}

bool AoeLogicMap::remove_runtime_static_obstacle(AoeObstacleId id) {
    const auto it = obstacle_lookup_.find(id);
    if (it == obstacle_lookup_.end() ||
        obstacles_[it->second].kind != AoeStaticObstacleKind::Runtime)
        return false;
    const std::size_t index = it->second;
    const std::size_t last = obstacles_.size() - 1;
    if (index != last) {
        obstacles_[index] = std::move(obstacles_[last]);
        obstacle_lookup_[obstacles_[index].id] = index;
    }
    obstacles_.pop_back();
    obstacle_lookup_.erase(it);
    ++static_revision_;
    rebuild_static_index();
    return true;
}

const AoeStaticObstacleDesc* AoeLogicMap::static_obstacle(AoeObstacleId id) const {
    const auto it = obstacle_lookup_.find(id);
    return it == obstacle_lookup_.end() ? nullptr : &obstacles_[it->second].desc;
}

void AoeLogicMap::visit_static_obstacles(const std::function<void(
    AoeObstacleId, const AoeStaticObstacleDesc&)>& visitor) const {
    if (!visitor) return;
    for (const auto& obstacle : obstacles_)
        visitor(obstacle.id, obstacle.desc);
}

std::optional<AoeStaticObstacleKind> AoeLogicMap::static_obstacle_kind(
    AoeObstacleId id) const {
    const auto it = obstacle_lookup_.find(id);
    return it == obstacle_lookup_.end()
        ? std::nullopt
        : std::optional<AoeStaticObstacleKind>(obstacles_[it->second].kind);
}

std::size_t AoeLogicMap::cell_index(int x, int y) const {
    return static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x);
}

bool AoeLogicMap::obstacle_cell_bounds(
    const AoeStaticObstacleDesc& obstacle, int& min_x, int& min_y,
    int& max_x, int& max_y) const {
    const glm::vec2 extent = obstacle.shape == AoeStaticObstacleShape::Aabb
        ? obstacle.half_extents : glm::vec2(obstacle.radius);
    const glm::vec2 obstacle_min = obstacle.center - extent;
    const glm::vec2 obstacle_max = obstacle.center + extent;
    const glm::vec2 map_max = origin_ + glm::vec2(
        width_ * tile_size_, height_ * tile_size_);
    if (obstacle_max.x < origin_.x || obstacle_max.y < origin_.y ||
        obstacle_min.x > map_max.x || obstacle_min.y > map_max.y)
        return false;
    const glm::vec2 low = (obstacle_min - origin_) / tile_size_;
    const glm::vec2 high = (obstacle_max - origin_) / tile_size_;
    min_x = std::clamp(static_cast<int>(std::floor(low.x)), 0,
                       static_cast<int>(width_) - 1);
    min_y = std::clamp(static_cast<int>(std::floor(low.y)), 0,
                       static_cast<int>(height_) - 1);
    max_x = std::clamp(static_cast<int>(std::floor(high.x)), 0,
                       static_cast<int>(width_) - 1);
    max_y = std::clamp(static_cast<int>(std::floor(high.y)), 0,
                       static_cast<int>(height_) - 1);
    return true;
}

void AoeLogicMap::rebuild_static_index() {
    for (auto& bucket : static_buckets_) bucket.clear();
    static_query_marks_.assign(obstacles_.size(), 0);
    static_query_generation_ = 0;
    for (std::size_t index = 0; index < obstacles_.size(); ++index) {
        int min_x, min_y, max_x, max_y;
        if (!obstacle_cell_bounds(obstacles_[index].desc,
                                  min_x, min_y, max_x, max_y))
            continue;
        for (int y = min_y; y <= max_y; ++y)
            for (int x = min_x; x <= max_x; ++x)
                static_buckets_[cell_index(x, y)].push_back(index);
    }
}

bool AoeLogicMap::position_blocked(glm::vec2 point, glm::vec2 clearance) const {
    if (!contains(point, clearance)) return true;
    const glm::vec2 low = point - clearance;
    const glm::vec2 high = point + clearance;
    const auto min_cell = world_to_cell(glm::max(low, origin_));
    const glm::vec2 map_max = origin_ + glm::vec2(width_ * tile_size_,
                                                  height_ * tile_size_);
    const auto max_cell = world_to_cell(glm::min(
        high, map_max - glm::vec2(Epsilon)));
    if (!min_cell || !max_cell) return true;
    if (++static_query_generation_ == 0) {
        std::fill(static_query_marks_.begin(), static_query_marks_.end(), 0);
        static_query_generation_ = 1;
    }
    for (int y = min_cell->y; y <= max_cell->y; ++y)
        for (int x = min_cell->x; x <= max_cell->x; ++x)
            for (const auto index : static_buckets_[cell_index(x, y)]) {
                if (static_query_marks_[index] == static_query_generation_)
                    continue;
                static_query_marks_[index] = static_query_generation_;
                const auto& obstacle = obstacles_[index].desc;
                if (obstacle.shape == AoeStaticObstacleShape::Aabb) {
                    const glm::vec2 half = obstacle.half_extents + clearance;
                    const glm::vec2 delta = glm::abs(point - obstacle.center);
                    if (delta.x <= half.x && delta.y <= half.y) return true;
                } else {
                    const glm::vec2 radii = glm::vec2(obstacle.radius) + clearance;
                    const glm::vec2 delta = point - obstacle.center;
                    if (delta.x * delta.x / (radii.x * radii.x) +
                        delta.y * delta.y / (radii.y * radii.y) <= 1.f)
                        return true;
                }
            }
    return false;
}

bool AoeLogicMap::cell_traversable(int x, int y, glm::vec2 clearance) const {
    if (x < 0 || y < 0 || x >= static_cast<int>(width_) ||
        y >= static_cast<int>(height_)) return false;
    return !position_blocked(cell_center(x, y), clearance);
}

float AoeLogicMap::static_safe_fraction(glm::vec2 from, glm::vec2 to,
                                        glm::vec2 clearance) const {
    if (!contains(from, clearance)) return 0.f;
    float result = contains(to, clearance) ? 1.f : 0.f;
    const glm::vec2 map_max = origin_ + glm::vec2(
        width_ * tile_size_, height_ * tile_size_) - glm::vec2(Epsilon);
    const glm::vec2 low = glm::clamp(
        glm::min(from, to) - clearance, origin_, map_max);
    const glm::vec2 high = glm::clamp(
        glm::max(from, to) + clearance, origin_, map_max);
    const auto min_cell = world_to_cell(low);
    const auto max_cell = world_to_cell(high);
    if (!min_cell || !max_cell) return result;
    if (++static_query_generation_ == 0) {
        std::fill(static_query_marks_.begin(), static_query_marks_.end(), 0);
        static_query_generation_ = 1;
    }
    for (int y = min_cell->y; y <= max_cell->y; ++y)
        for (int x = min_cell->x; x <= max_cell->x; ++x)
            for (const auto index : static_buckets_[cell_index(x, y)]) {
                if (static_query_marks_[index] == static_query_generation_)
                    continue;
                static_query_marks_[index] = static_query_generation_;
                const auto& record = obstacles_[index];
                float enter = 1.f;
                if (record.desc.shape == AoeStaticObstacleShape::Aabb)
                    enter = aabb_segment_enter(
                        from, to, record.desc.center,
                        record.desc.half_extents + clearance);
                else
                    enter = ellipse_segment_enter(
                        from, to, record.desc.center,
                        glm::vec2(record.desc.radius) + clearance);
                result = std::min(result, before_contact(enter));
            }
    return result;
}

void AoeDynamicObstacleIndex::reset(const AoeLogicMap& map) {
    cell_count_ = static_cast<std::size_t>(map.width()) * map.height();
    entries_.clear();
    pending_memberships_.clear();
    flat_dirty_ = true;
    diagnostics_.units_indexed = 0;
    diagnostics_.cell_memberships = 0;
    ++diagnostics_.rebuilds;
}

void AoeDynamicObstacleIndex::clear() {
    entries_.clear();
    pending_memberships_.clear();
    cell_offsets_.clear();
    build_cursors_.clear();
    members_.clear();
    cell_count_ = 0;
    flat_dirty_ = false;
    diagnostics_.units_indexed = 0;
    diagnostics_.cell_memberships = 0;
}

void AoeDynamicObstacleIndex::insert(
    const AoeLogicMap& map, const AoeDynamicObstacleEntry& entry) {
    if (!map.valid() || entry.entity == entt::null ||
        !(entry.radii.x > 0.f) || !(entry.radii.y > 0.f)) return;
    const glm::vec2 low = glm::max(entry.center - entry.radii, map.origin());
    const glm::vec2 map_max = map.origin() + glm::vec2(
        map.width() * map.tile_size(), map.height() * map.tile_size());
    const glm::vec2 high = glm::min(
        entry.center + entry.radii, map_max - glm::vec2(Epsilon));
    const auto min_cell = map.world_to_cell(low);
    const auto max_cell = map.world_to_cell(high);
    if (!min_cell || !max_cell) return;
    const std::size_t index = entries_.size();
    entries_.push_back(entry);
    flat_dirty_ = true;
    ++diagnostics_.units_indexed;
    for (int y = min_cell->y; y <= max_cell->y; ++y)
        for (int x = min_cell->x; x <= max_cell->x; ++x) {
            pending_memberships_.push_back({
                static_cast<std::uint32_t>(
                    static_cast<std::size_t>(y) * map.width() + x),
                static_cast<std::uint32_t>(index)});
            ++diagnostics_.cell_memberships;
        }
}

void AoeDynamicObstacleIndex::finalize(const AoeLogicMap& map) const {
    if (!flat_dirty_) return;
    const std::size_t expected = static_cast<std::size_t>(map.width()) *
                                 map.height();
    cell_offsets_.assign(expected + 1, 0);
    for (const auto& membership : pending_memberships_)
        if (membership.cell < expected)
            ++cell_offsets_[static_cast<std::size_t>(membership.cell) + 1];
    for (std::size_t i = 1; i < cell_offsets_.size(); ++i)
        cell_offsets_[i] += cell_offsets_[i - 1];
    members_.resize(cell_offsets_.back());
    build_cursors_ = cell_offsets_;
    for (const auto& membership : pending_memberships_)
        if (membership.cell < expected)
            members_[build_cursors_[membership.cell]++] = membership.entry;
    query_marks_.resize(entries_.size(), 0);
    flat_dirty_ = false;
}

float AoeDynamicObstacleIndex::dynamic_safe_fraction(
    const AoeLogicMap& map, glm::vec2 from, glm::vec2 to, glm::vec2 radii,
    entt::entity self, entt::entity ignored_squad) const {
    return dynamic_safe_fraction(map, from, to, radii, self,
                                 ignored_squad, entt::null);
}

float AoeDynamicObstacleIndex::dynamic_safe_fraction(
    const AoeLogicMap& map, glm::vec2 from, glm::vec2 to, glm::vec2 radii,
    entt::entity self, entt::entity ignored_squad,
    entt::entity ignored_entity) const {
    float result = 1.f;
    const glm::vec2 low = glm::min(from, to) - radii;
    const glm::vec2 high = glm::max(from, to) + radii;
    query(map, low, high, [&](const AoeDynamicObstacleEntry& obstacle) {
        if (obstacle.entity == self || obstacle.entity == ignored_entity ||
            (ignored_squad != entt::null && obstacle.squad == ignored_squad))
            return;
        const float enter = ellipse_segment_enter(
            from, to, obstacle.center, radii + obstacle.radii);
        result = std::min(result, before_contact(enter));
    });
    return result;
}

bool AoeDynamicObstacleIndex::cell_occupied(
    const AoeLogicMap& map, int x, int y, glm::vec2 clearance,
    entt::entity self, entt::entity ignored_squad) const {
    return cell_occupied(map, x, y, clearance, self,
                         ignored_squad, entt::null);
}

bool AoeDynamicObstacleIndex::cell_occupied(
    const AoeLogicMap& map, int x, int y, glm::vec2 clearance,
    entt::entity self, entt::entity ignored_squad,
    entt::entity ignored_entity) const {
    const glm::vec2 point = map.cell_center(x, y);
    bool occupied = false;
    query(map, point - clearance, point + clearance,
          [&](const AoeDynamicObstacleEntry& obstacle) {
        if (occupied || obstacle.entity == self ||
            obstacle.entity == ignored_entity ||
            (ignored_squad != entt::null && obstacle.squad == ignored_squad))
            return;
        const glm::vec2 radii = clearance + obstacle.radii;
        const glm::vec2 delta = point - obstacle.center;
        occupied = delta.x * delta.x / (radii.x * radii.x) +
                   delta.y * delta.y / (radii.y * radii.y) <= 1.f;
    });
    return occupied;
}

} // namespace gld::ecs::aoe
