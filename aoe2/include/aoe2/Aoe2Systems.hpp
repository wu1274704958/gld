#pragma once

#include "Aoe2Components.hpp"
#include "Aoe2ResourceManager.hpp"
#include <array>
#include <unordered_map>
#include <vector>
#include <program.hpp>
#include <ecs/render/Batch.hpp>
#include <ecs/Components.hpp>

namespace gld::ecs::aoe2 {

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
};

entt::entity spawn_aoe2_unit(EcsWorld& world, const SpawnOptions& options,
                             const Transform& transform = {});
bool request_aoe2_animation(Aoe2UnitRender& unit, const std::string& animation_name);
bool request_aoe2_animation(Aoe2UnitRender& unit, AnimationSlot animation_slot);
void restart_aoe2_animation(Aoe2UnitRender& unit);
void request_animation_residency(AssetServer& server, Aoe2UnitAppearance& appearance,
                                 const std::string& animation_name);
void request_animation_residency(AssetServer& server, Aoe2UnitAppearance& appearance,
                                 AnimationSlot animation_slot);
void spawn_aoe2_unit_system(EcsWorld& world);
void aoe2_unit_animation_system(EcsWorld& world);
void register_aoe2_batch_lifecycle(EcsWorld& world);
void aoe2_batch_system(EcsWorld& world);
void destroy_aoe2_batches(EcsWorld& world);

} // namespace gld::ecs::aoe2
