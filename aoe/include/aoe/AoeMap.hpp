#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <ecs/assets/Loader.hpp>

namespace gld::ecs::aoe {

using AoeObstacleId = std::uint64_t;

enum class AoeStaticObstacleShape { Aabb, Circle };

struct AoeStaticObstacleDesc {
    std::string source_id;
    AoeStaticObstacleShape shape = AoeStaticObstacleShape::Aabb;
    glm::vec2 center{0.f};
    glm::vec2 half_extents{0.f};
    float radius = 0.f;
};

struct AoeMapDefinition {
    std::string id;
    glm::vec2 origin{0.f};
    float tile_size = 1.f;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> heights;
    std::vector<AoeStaticObstacleDesc> static_obstacles;
};

struct AoeMapDefinitionDesc
    : BaseAssetDesc<AoeMapDefinitionDesc, std::string> {
    using Asset = AoeMapDefinition;
    using BaseAssetDesc::BaseAssetDesc;
    const std::string& path() const { return get<0>(); }
};

struct AoeMapDefinitionLoader : IAssetLoader<AoeMapDefinitionDesc> {
    std::shared_ptr<void> load_cpu(const AoeMapDefinitionDesc&,
                                  const IFileSystem&) override;
    std::shared_ptr<AoeMapDefinition> finalize(
        std::shared_ptr<void>, const AoeMapDefinitionDesc&) override;
};

struct AoeMapCell { int x = -1; int y = -1; };

class AoeLogicMap {
public:
    AoeLogicMap() = default;
    explicit AoeLogicMap(const AoeMapDefinition& definition);

    bool reset(const AoeMapDefinition& definition, std::string* error = nullptr);
    bool valid() const { return width_ > 0 && height_ > 0 && tile_size_ > 0.f; }
    const std::string& id() const { return id_; }
    glm::vec2 origin() const { return origin_; }
    float tile_size() const { return tile_size_; }
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    std::uint64_t static_revision() const { return static_revision_; }

    bool contains(glm::vec2 point, glm::vec2 clearance = {0.f, 0.f}) const;
    std::optional<AoeMapCell> world_to_cell(glm::vec2 point) const;
    glm::vec2 cell_center(int x, int y) const;
    std::optional<float> sample_height(glm::vec2 point) const;

    AoeObstacleId add_static_obstacle(const AoeStaticObstacleDesc& obstacle);
    bool update_static_obstacle(AoeObstacleId,
                                const AoeStaticObstacleDesc& obstacle);
    bool remove_static_obstacle(AoeObstacleId);
    const AoeStaticObstacleDesc* static_obstacle(AoeObstacleId) const;
    std::size_t static_obstacle_count() const { return obstacles_.size(); }

    bool position_blocked(glm::vec2 point, glm::vec2 clearance) const;
    bool cell_traversable(int x, int y, glm::vec2 clearance) const;
    float static_safe_fraction(glm::vec2 from, glm::vec2 to,
                               glm::vec2 clearance) const;

private:
    struct ObstacleRecord {
        AoeObstacleId id = 0;
        AoeStaticObstacleDesc desc;
    };

    std::size_t cell_index(int x, int y) const;
    void rebuild_static_index();
    bool obstacle_cell_bounds(const AoeStaticObstacleDesc&, int& min_x,
                              int& min_y, int& max_x, int& max_y) const;

    std::string id_;
    glm::vec2 origin_{0.f};
    float tile_size_ = 0.f;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::vector<float> heights_;
    std::vector<ObstacleRecord> obstacles_;
    std::unordered_map<AoeObstacleId, std::size_t> obstacle_lookup_;
    std::vector<std::vector<std::size_t>> static_buckets_;
    mutable std::vector<std::uint32_t> static_query_marks_;
    mutable std::uint32_t static_query_generation_ = 0;
    AoeObstacleId next_obstacle_id_ = 1;
    std::uint64_t static_revision_ = 1;
};

struct AoeDynamicObstacleEntry {
    entt::entity entity{entt::null};
    std::uint64_t instance_id = 0;
    entt::entity squad{entt::null};
    glm::vec2 center{0.f};
    glm::vec2 radii{0.f};
    glm::vec2 velocity{0.f};
};

struct AoeDynamicObstacleDiagnostics {
    std::uint64_t rebuilds = 0;
    std::uint64_t units_indexed = 0;
    std::uint64_t cell_memberships = 0;
    std::uint64_t queries = 0;
    std::uint64_t candidates = 0;
};

class AoeDynamicObstacleIndex {
public:
    void reset(const AoeLogicMap& map);
    void clear();
    void reserve(std::size_t count) {
        entries_.reserve(count);
        pending_memberships_.reserve(count * 4u);
    }
    void insert(const AoeLogicMap& map, const AoeDynamicObstacleEntry& entry);
    // Completes the two-pass flat-cell build. Calling query() also finalizes
    // lazily, but fixed-tick code should call this once after all inserts.
    void finalize(const AoeLogicMap& map) const;

    const std::vector<AoeDynamicObstacleEntry>& entries() const {
        return entries_;
    }
    const AoeDynamicObstacleDiagnostics& diagnostics() const {
        return diagnostics_;
    }
    AoeDynamicObstacleDiagnostics& diagnostics() { return diagnostics_; }

    template<class Fn>
    void query(const AoeLogicMap& map, glm::vec2 min, glm::vec2 max,
               Fn&& function) const {
        ++diagnostics_.queries;
        finalize(map);
        if (!map.valid() || cell_offsets_.empty()) return;
        const auto min_cell = map.world_to_cell(glm::max(min, map.origin()));
        const glm::vec2 map_max = map.origin() + glm::vec2(
            map.width() * map.tile_size(), map.height() * map.tile_size());
        const auto max_cell = map.world_to_cell(glm::min(
            max, map_max - glm::vec2(0.00001f)));
        if (!min_cell || !max_cell) return;
        if (query_marks_.size() < entries_.size())
            query_marks_.resize(entries_.size(), 0);
        if (++query_generation_ == 0) {
            std::fill(query_marks_.begin(), query_marks_.end(), 0);
            query_generation_ = 1;
        }
        for (int y = min_cell->y; y <= max_cell->y; ++y)
            for (int x = min_cell->x; x <= max_cell->x; ++x) {
                const auto cell = static_cast<std::size_t>(y) * map.width() +
                                  static_cast<std::size_t>(x);
                for (std::size_t member = cell_offsets_[cell];
                     member < cell_offsets_[cell + 1]; ++member) {
                    const auto index = members_[member];
                    if (query_marks_[index] == query_generation_) continue;
                    query_marks_[index] = query_generation_;
                    ++diagnostics_.candidates;
                    function(entries_[index]);
                }
            }
    }

    float dynamic_safe_fraction(const AoeLogicMap& map, glm::vec2 from,
                                glm::vec2 to, glm::vec2 radii,
                                entt::entity self,
                                entt::entity ignored_squad = entt::null) const;
    float dynamic_safe_fraction(const AoeLogicMap& map, glm::vec2 from,
                                glm::vec2 to, glm::vec2 radii,
                                entt::entity self, entt::entity ignored_squad,
                                entt::entity ignored_entity) const;
    bool cell_occupied(const AoeLogicMap& map, int x, int y,
                       glm::vec2 clearance, entt::entity self,
                       entt::entity ignored_squad = entt::null) const;
    bool cell_occupied(const AoeLogicMap& map, int x, int y,
                       glm::vec2 clearance, entt::entity self,
                       entt::entity ignored_squad,
                       entt::entity ignored_entity) const;

private:
    struct PendingMembership {
        std::uint32_t cell = 0;
        std::uint32_t entry = 0;
    };
    std::vector<AoeDynamicObstacleEntry> entries_;
    std::vector<PendingMembership> pending_memberships_;
    mutable std::vector<std::size_t> cell_offsets_;
    mutable std::vector<std::size_t> build_cursors_;
    mutable std::vector<std::size_t> members_;
    mutable bool flat_dirty_ = true;
    std::size_t cell_count_ = 0;
    mutable std::vector<std::uint32_t> query_marks_;
    mutable std::uint32_t query_generation_ = 0;
    mutable AoeDynamicObstacleDiagnostics diagnostics_;
};

} // namespace gld::ecs::aoe
