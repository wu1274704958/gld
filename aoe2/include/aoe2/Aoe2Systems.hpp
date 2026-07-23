#pragma once

#include "Aoe2Components.hpp"
#include "Aoe2ResourceManager.hpp"
#include <array>
#include <unordered_map>
#include <program.hpp>
#include <ecs/render/Batch.hpp>
#include <ecs/Components.hpp>

namespace gld::ecs::aoe2 {

struct Aoe2BatchIndex {
    std::unordered_map<BatchKey, entt::entity, BatchKeyHash> map;
};

struct Aoe2RenderResources {
    Handle<Program> sprite_shader;
    Handle<Program> player_color_shader;
    Handle<Program> shadow_shader;
    std::shared_ptr<Texture<TexType::D2>> palette_texture;
    std::uint64_t palette_revision = 1;
};

entt::entity spawn_aoe2_unit(EcsWorld& world, const SpawnOptions& options,
                             const Transform& transform = {});
bool request_aoe2_animation(Aoe2UnitRender& unit, const std::string& animation_name);
void restart_aoe2_animation(Aoe2UnitRender& unit);
void request_animation_residency(AssetServer& server, Aoe2UnitAppearance& appearance,
                                 const std::string& animation_name);
void spawn_aoe2_unit_system(EcsWorld& world);
void aoe2_unit_animation_system(EcsWorld& world);
void aoe2_batch_system(EcsWorld& world);
void destroy_aoe2_batches(EcsWorld& world);

} // namespace gld::ecs::aoe2
