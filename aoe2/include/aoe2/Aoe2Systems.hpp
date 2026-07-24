#pragma once

#include "Aoe2Components.hpp"
#include "Aoe2ResourceManager.hpp"
#include <array>
#include <unordered_map>
#include <vector>
#include <program.hpp>
#include <ecs/render/Batch.hpp>
#include <ecs/render/RenderPassExec.hpp>
#include <ecs/Components.hpp>
#include <ecs/PerformanceMonitoring.hpp>

namespace gld::ecs::aoe2 {

inline constexpr RegisteredRenderPassId Aoe2UnitPassId = 0xA0E20001u;

struct Aoe2WorldInstance {
    glm::vec4 axis_x{1.f, 0.f, 0.f, 0.f};
    glm::vec4 axis_y{0.f, 1.f, 0.f, 0.f};
    glm::vec4 origin{0.f, 0.f, 0.f, 1.f};
};

struct Aoe2VisualInstance {
    glm::vec4 uv{0.f};
    glm::vec4 size_and_foot{0.f};
    std::uint32_t tint_rgba8 = 0xFFFFFFFFu;
    std::uint32_t player_debug_flags = 0;
};

static_assert(sizeof(Aoe2WorldInstance) == 48);
static_assert(sizeof(Aoe2VisualInstance) == 40);

struct Aoe2BatchComponent {
    Aoe2BatchComponent() = default;
    ~Aoe2BatchComponent();
    Aoe2BatchComponent(const Aoe2BatchComponent&) = delete;
    Aoe2BatchComponent& operator=(const Aoe2BatchComponent&) = delete;
    Aoe2BatchComponent(Aoe2BatchComponent&&) noexcept;
    Aoe2BatchComponent& operator=(Aoe2BatchComponent&&) noexcept;

    BatchKey key;
    std::uint32_t layers = 0xFFFFFFFFu;
    Program* program = nullptr;
    std::array<std::shared_ptr<void>, MaxBatchTextures> texture_refs{};
    std::array<int, MaxBatchTextures> sampler_locations{-1, -1, -1, -1};
    int order = 0;

    std::vector<Aoe2WorldInstance> world_instances;
    std::vector<Aoe2VisualInstance> visual_instances;
    std::vector<BatchUploadRange> world_dirty_ranges;
    std::vector<BatchUploadRange> visual_dirty_ranges;
    bool world_full_dirty = true;
    bool visual_full_dirty = true;

    unsigned int vao = 0;
    unsigned int world_vbo = 0;
    unsigned int visual_vbo = 0;
    std::size_t world_gpu_cap = 0;
    std::size_t visual_gpu_cap = 0;
};

struct Aoe2BatchMember {
    entt::entity entity = entt::null;
    Aoe2BatchSourceKind kind = Aoe2BatchSourceKind::Main;
};

struct Aoe2BatchGroup {
    BatchKey key;
    entt::entity batch_entity = entt::null;
    std::vector<Aoe2BatchMember> members;
    bool active = false;
};

struct Aoe2BatchIndex {
    std::unordered_map<BatchKey, std::uint32_t, BatchKeyHash> key_to_group;
    std::vector<Aoe2BatchGroup> groups;
    std::vector<std::uint32_t> free_group_ids;
    bool lifecycle_connected = false;
};

enum class Aoe2RenderDirty : std::uint8_t {
    None = 0,
    Topology = 1u << 0,
    Frame = 1u << 1,
    Transform = 1u << 2,
    Material = 1u << 3,
    Full = 0x0Fu
};

inline Aoe2RenderDirty operator|(Aoe2RenderDirty lhs, Aoe2RenderDirty rhs) {
    return static_cast<Aoe2RenderDirty>(
        static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

struct Aoe2DirtyEntry {
    entt::entity entity = entt::null;
    Aoe2RenderDirty mask = Aoe2RenderDirty::None;
};

struct Aoe2DirtyQueue {
    std::vector<Aoe2DirtyEntry> pending;
    std::vector<std::uint8_t> pending_masks;
    std::vector<entt::entity> pending_entities;
    std::vector<Aoe2DirtyEntry> processing;
    std::vector<std::uint8_t> processing_masks;
    std::vector<entt::entity> processing_entities;
    std::uint32_t enqueued = 0;
    std::uint32_t deduplicated = 0;

    void enqueue(entt::entity entity, Aoe2RenderDirty dirty) {
        if (entity == entt::null || dirty == Aoe2RenderDirty::None) return;
        GLD_PERF_MONITOR(++enqueued);
        const auto index = static_cast<std::size_t>(entt::to_entity(entity));
        if (index >= pending_masks.size()) {
            pending_masks.resize(index + 1u, 0u);
            pending_entities.resize(index + 1u, entt::null);
        }
        auto& mask = pending_masks[index];
        if (mask != 0u && pending_entities[index] == entity) {
            mask |= static_cast<std::uint8_t>(dirty);
            GLD_PERF_MONITOR(++deduplicated);
            return;
        }
        mask = static_cast<std::uint8_t>(dirty);
        pending_entities[index] = entity;
        pending.push_back({entity, dirty});
    }

    bool contains(entt::entity entity) const {
        if (entity == entt::null) return false;
        const auto index = static_cast<std::size_t>(entt::to_entity(entity));
        return index < pending_masks.size() && pending_masks[index] != 0u &&
            pending_entities[index] == entity;
    }

    void begin_processing() {
        processing.clear();
        processing.swap(pending);
        processing_masks.swap(pending_masks);
        processing_entities.swap(pending_entities);
    }

    Aoe2RenderDirty processing_dirty(entt::entity entity) const {
        if (entity == entt::null) return Aoe2RenderDirty::None;
        const auto index = static_cast<std::size_t>(entt::to_entity(entity));
        if (index >= processing_masks.size() || processing_entities[index] != entity)
            return Aoe2RenderDirty::None;
        return static_cast<Aoe2RenderDirty>(processing_masks[index]);
    }

    void end_processing() {
        for (const auto& entry : processing) {
            const auto index = static_cast<std::size_t>(entt::to_entity(entry.entity));
            if (index < processing_masks.size() &&
                processing_entities[index] == entry.entity) {
                processing_masks[index] = 0u;
                processing_entities[index] = entt::null;
            }
        }
        processing.clear();
    }
};

struct Aoe2RenderResources {
    Handle<Program> sprite_shader;
    Handle<Program> player_color_shader;
    Handle<Program> shadow_shader;
    std::shared_ptr<Texture<TexType::D2>> palette_texture;
    std::uint64_t palette_revision = 1;
    unsigned int sprite_sampler_program = 0;
    unsigned int player_sampler_program = 0;
    unsigned int shadow_sampler_program = 0;
    int sprite_diffuse_sampler = -1;
    int player_diffuse_sampler = -1;
    int player_mask_sampler = -1;
    int player_palette_sampler = -1;
    int shadow_diffuse_sampler = -1;
    unsigned int quad_vbo = 0;
    unsigned int quad_ebo = 0;
    std::array<unsigned int, 4> gpu_time_queries{};
    std::array<bool, 4> gpu_time_pending{};
    std::uint32_t gpu_time_write = 0;
    bool quad_ready = false;
};

struct Aoe2PerformanceDiagnostics {
    double animation_ms = 0.0;
    double batch_total_ms = 0.0;
    double batch_group_ms = 0.0;
    double batch_rebuild_ms = 0.0;
    double membership_sync_ms = 0.0;
    double instance_update_ms = 0.0;
    std::uint32_t animation_units = 0;
    std::uint32_t animation_frame_changes = 0;
    std::uint32_t animation_pending = 0;
    std::uint32_t batch_units = 0;
    std::uint32_t batch_sources = 0;
    std::uint32_t batch_groups = 0;
    std::uint32_t batch_dirty_groups = 0;
    std::uint32_t batch_unchanged_groups = 0;
    std::uint32_t batch_rebuilt_instances = 0;
    std::uint32_t membership_attaches = 0;
    std::uint32_t membership_detaches = 0;
    std::uint32_t membership_migrations = 0;
    std::uint32_t group_creates = 0;
    std::uint32_t group_destroys = 0;
    std::uint32_t frame_dirty_instances = 0;
    std::uint32_t transform_dirty_instances = 0;
    std::uint32_t material_dirty_instances = 0;
    std::uint32_t full_initialized_instances = 0;
    std::uint32_t unchanged_instances = 0;
    std::uint32_t dirty_enqueued = 0;
    std::uint32_t dirty_deduplicated = 0;
    std::uint32_t dirty_audit_violations = 0;
    bool dirty_dense_mode = false;
    std::uint64_t world_upload_bytes = 0;
    std::uint64_t visual_upload_bytes = 0;
    std::uint32_t world_uploads = 0;
    std::uint32_t visual_uploads = 0;
    std::uint32_t upload_ranges = 0;
    double render_prepare_ms = 0.0;
    double render_upload_ms = 0.0;
    double render_submit_ms = 0.0;
    double render_gpu_ms = 0.0;
    std::uint32_t render_gpu_query_skips = 0;
};

entt::entity spawn_aoe2_unit(EcsWorld& world, const SpawnOptions& options,
                             const Transform& transform = {});
bool request_aoe2_animation(Aoe2UnitRender& unit, const std::string& animation_name);
bool request_aoe2_animation(Aoe2UnitRender& unit, AnimationSlot animation_slot);
void restart_aoe2_animation(Aoe2UnitRender& unit);
bool request_aoe2_animation(EcsWorld& world, entt::entity entity,
                            AnimationSlot animation_slot);
bool request_aoe2_animation(EcsWorld& world, entt::entity entity,
                            const std::string& animation_name);
void restart_aoe2_animation(EcsWorld& world, entt::entity entity);
void set_aoe2_player_color(EcsWorld& world, entt::entity entity,
                           int player_color, int debug_mode);
void set_aoe2_tint(EcsWorld& world, entt::entity entity, glm::vec4 tint);
void set_aoe2_visible(EcsWorld& world, entt::entity entity, bool visible);
void set_aoe2_playing(EcsWorld& world, entt::entity entity, bool playing);
void set_aoe2_direction(EcsWorld& world, entt::entity entity,
                        int direction_slot, int direction_slot_count = 0);
void mark_aoe2_render_dirty(EcsWorld& world, entt::entity entity,
                            Aoe2RenderDirty dirty);
void request_animation_residency(AssetServer& server, Aoe2UnitAppearance& appearance,
                                 const std::string& animation_name);
void request_animation_residency(AssetServer& server, Aoe2UnitAppearance& appearance,
                                 AnimationSlot animation_slot);
void spawn_aoe2_unit_system(EcsWorld& world);
void aoe2_unit_animation_system(EcsWorld& world);
void register_aoe2_batch_lifecycle(EcsWorld& world);
void aoe2_batch_system(EcsWorld& world);
void destroy_aoe2_batches(EcsWorld& world);
RegisteredRenderPass make_aoe2_unit_pass();
void render_aoe2_unit_pass(RenderPassContext& context,
                           const RegisteredRenderPass& pass,
                           const ResolvedRenderPassState& state);

} // namespace gld::ecs::aoe2
