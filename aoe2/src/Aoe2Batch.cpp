#include <aoe2/Aoe2Systems.hpp>

#include <chrono>
#include <cstdint>

#include <glm/gtc/matrix_transform.hpp>
#include <ecs/Components.hpp>
#include <ecs/render/BatchSystem.hpp>
#include <ecs/render/RenderComponents.hpp>

namespace gld::ecs::aoe2 {
namespace {

struct DesiredBatch {
    bool valid = false;
    Aoe2BatchSourceKind kind = Aoe2BatchSourceKind::Main;
    BatchKey key;
    Program* program = nullptr;
    std::array<int, MaxBatchTextures> sampler_locations{-1, -1, -1, -1};
    const Layer* diffuse_layer = nullptr;
    const Layer* player_layer = nullptr;
    const std::shared_ptr<Texture<TexType::D2>>* palette_texture = nullptr;
    int order = 0;
};

inline constexpr std::uint8_t FrameDirty = 1u << 0;
inline constexpr std::uint8_t TransformDirty = 1u << 1;
inline constexpr std::uint8_t MaterialDirty = 1u << 2;

int sampler(Program& program, const char* name) {
    int location = program.uniform_id(name);
    if (location < 0) {
        program.locat_uniforms(name);
        location = program.uniform_id(name);
    }
    return location;
}

void refresh_sampler_locations(Aoe2RenderResources& resources, Program& sprite,
                               Program& player, Program& shadow) {
    const auto sprite_id = static_cast<unsigned int>(sprite);
    if (resources.sprite_sampler_program != sprite_id) {
        resources.sprite_sampler_program = sprite_id;
        resources.sprite_diffuse_sampler = sampler(sprite, "diffuseTex");
    }
    const auto player_id = static_cast<unsigned int>(player);
    if (resources.player_sampler_program != player_id) {
        resources.player_sampler_program = player_id;
        resources.player_diffuse_sampler = sampler(player, "diffuseTex");
        resources.player_mask_sampler = sampler(player, "playerColorMask");
        resources.player_palette_sampler = sampler(player, "playerPalette");
    }
    const auto shadow_id = static_cast<unsigned int>(shadow);
    if (resources.shadow_sampler_program != shadow_id) {
        resources.shadow_sampler_program = shadow_id;
        resources.shadow_diffuse_sampler = sampler(shadow, "diffuseTex");
    }
}

glm::mat4 frame_transform(const glm::mat4& world, const Frame& frame) {
    const glm::vec3 offset(frame.width * .5f - frame.foot.x,
                           frame.foot.y - frame.height * .5f, 0.f);
    return world * glm::translate(glm::mat4(1.f), offset)
        * glm::scale(glm::mat4(1.f), glm::vec3(frame.width, frame.height, 1.f));
}

Aoe2BatchGroup* active_group(Aoe2BatchIndex& index, std::uint32_t group_id) {
    if (group_id >= index.groups.size()) return nullptr;
    auto& group = index.groups[group_id];
    return group.active ? &group : nullptr;
}

const Aoe2BatchGroup* active_group(const Aoe2BatchIndex& index, std::uint32_t group_id) {
    if (group_id >= index.groups.size()) return nullptr;
    const auto& group = index.groups[group_id];
    return group.active ? &group : nullptr;
}

Aoe2BatchSlot& membership_slot(Aoe2UnitBatchMembership& membership,
                               Aoe2BatchSourceKind kind) {
    return kind == Aoe2BatchSourceKind::Shadow ? membership.shadow : membership.main;
}

void destroy_group(entt::registry& reg, Aoe2BatchIndex& index, std::uint32_t group_id,
                   Aoe2PerformanceDiagnostics* performance) {
    auto* group = active_group(index, group_id);
    if (!group) return;
    index.key_to_group.erase(group->key);
    if (reg.valid(group->batch_entity)) reg.destroy(group->batch_entity);
    group->members.clear();
    group->batch_entity = entt::null;
    group->active = false;
    index.free_group_ids.push_back(group_id);
    if (performance) ++performance->group_destroys;
    if (auto* diagnostics = reg.ctx().find<RenderDiagnostics>())
        ++diagnostics->batch_groups_destroyed;
}

void detach_slot(entt::registry& reg, Aoe2BatchIndex& index, entt::entity owner,
                 Aoe2BatchSourceKind kind, Aoe2BatchSlot& slot,
                 Aoe2PerformanceDiagnostics* performance) {
    auto* group = active_group(index, slot.group_id);
    if (!slot.valid() || !group) {
        slot.reset();
        return;
    }

    std::size_t position = slot.instance_index;
    if (position >= group->members.size() || group->members[position].entity != owner ||
        group->members[position].kind != kind) {
        position = group->members.size();
        for (std::size_t i = 0; i < group->members.size(); ++i) {
            if (group->members[i].entity == owner && group->members[i].kind == kind) {
                position = i;
                break;
            }
        }
        if (position == group->members.size()) {
            slot.reset();
            return;
        }
    }

    auto& batch = reg.get<BatchComponent>(group->batch_entity);
    const auto detached_group_id = slot.group_id;
    const std::size_t last = group->members.size() - 1;
    if (position != last) {
        const auto moved = group->members[last];
        group->members[position] = moved;
        batch.instances[position] = std::move(batch.instances[last]);
        if (reg.valid(moved.entity)) {
            if (auto* moved_membership = reg.try_get<Aoe2UnitBatchMembership>(moved.entity)) {
                auto& moved_slot = membership_slot(*moved_membership, moved.kind);
                moved_slot.group_id = slot.group_id;
                moved_slot.instance_index = static_cast<std::uint32_t>(position);
            }
        }
        mark_batch_instance_dirty(batch, static_cast<std::uint32_t>(position));
    }
    group->members.pop_back();
    batch.instances.pop_back();
    batch.count = batch.instances.size();
    slot.reset();
    if (performance) ++performance->membership_detaches;
    if (group->members.empty()) destroy_group(reg, index, detached_group_id, performance);
}

std::uint32_t allocate_group_id(Aoe2BatchIndex& index) {
    if (!index.free_group_ids.empty()) {
        const auto id = index.free_group_ids.back();
        index.free_group_ids.pop_back();
        index.groups[id] = {};
        return id;
    }
    const auto id = static_cast<std::uint32_t>(index.groups.size());
    index.groups.emplace_back();
    return id;
}

std::uint32_t ensure_group(entt::registry& reg, Aoe2BatchIndex& index,
                           const DesiredBatch& desired,
                           Aoe2PerformanceDiagnostics& performance) {
    if (const auto found = index.key_to_group.find(desired.key);
        found != index.key_to_group.end()) {
        if (auto* group = active_group(index, found->second);
            group && reg.valid(group->batch_entity)) return found->second;
        index.key_to_group.erase(found);
    }

    const auto group_id = allocate_group_id(index);
    auto& group = index.groups[group_id];
    group.key = desired.key;
    group.batch_entity = reg.create();
    group.active = true;

    auto& batch = reg.emplace<BatchComponent>(group.batch_entity);
    batch.key = desired.key;
    batch.layers = desired.key.layers;
    batch.prog = desired.program;
    batch.order = desired.order;
    batch.used = true;
    batch.sampler_locations = desired.sampler_locations;
    if (desired.diffuse_layer)
        batch.texture_refs[0] = desired.diffuse_layer->texture.shared();
    if (desired.player_layer)
        batch.texture_refs[1] = desired.player_layer->texture.shared();
    if (desired.palette_texture)
        batch.texture_refs[2] = *desired.palette_texture;
    index.key_to_group.emplace(desired.key, group_id);
    ++performance.group_creates;
    return group_id;
}

void attach_slot(entt::registry& reg, Aoe2BatchIndex& index, entt::entity owner,
                 const DesiredBatch& desired, Aoe2BatchSlot& slot,
                 Aoe2PerformanceDiagnostics& performance) {
    const auto group_id = ensure_group(reg, index, desired, performance);
    auto& group = index.groups[group_id];
    auto& batch = reg.get<BatchComponent>(group.batch_entity);
    const auto position = static_cast<std::uint32_t>(group.members.size());
    group.members.push_back({owner, desired.kind});
    batch.instances.emplace_back();
    batch.count = batch.instances.size();
    batch.used = true;
    mark_batch_instance_dirty(batch, position);
    slot.group_id = group_id;
    slot.instance_index = position;
    slot.needs_full_update = true;
    ++performance.membership_attaches;
}

void synchronize_slot(entt::registry& reg, Aoe2BatchIndex& index, entt::entity owner,
                      const DesiredBatch& desired, Aoe2BatchSlot& slot,
                      Aoe2PerformanceDiagnostics& performance) {
    auto* current = active_group(index, slot.group_id);
    const bool current_valid = slot.valid() && current;
    if (!desired.valid) {
        if (current_valid) detach_slot(reg, index, owner, desired.kind, slot, &performance);
        else slot.reset();
        return;
    }
    if (current_valid && slot.instance_index < current->members.size() &&
        current->key == desired.key &&
        current->members[slot.instance_index].entity == owner &&
        current->members[slot.instance_index].kind == desired.kind) return;
    if (current_valid) {
        ++performance.membership_migrations;
        const auto kind = slot.instance_index < current->members.size()
            ? current->members[slot.instance_index].kind : desired.kind;
        detach_slot(reg, index, owner, kind, slot, &performance);
    } else {
        slot.reset();
    }
    attach_slot(reg, index, owner, desired, slot, performance);
}

void build_desired_batches(const Aoe2UnitRender& render, std::uint32_t layers,
                           Aoe2RenderResources& resources, Program& sprite_program,
                           Program& player_program, Program& shadow_program,
                           DesiredBatch& main, DesiredBatch& shadow) {
    main.kind = Aoe2BatchSourceKind::Main;
    shadow.kind = Aoe2BatchSourceKind::Shadow;
    if (!render.visible || !render.has_main) return;
    auto* appearance = render.appearance.get();
    if (!appearance) return;
    const auto* animation = appearance->animation_at(render.animation_slot);
    if (!animation) return;

    if (render.has_shadow) {
        if (auto* texture = animation->shadow.texture.get()) {
            shadow.valid = true;
            shadow.key.texture_count = 1;
            shadow.key.textures[0] = texture->get_id();
            shadow.key.shader = static_cast<unsigned int>(shadow_program);
            shadow.key.layers = layers;
            shadow.key.depth_write = BatchStateOverride::Disabled;
            shadow.program = &shadow_program;
            shadow.diffuse_layer = &animation->shadow;
            shadow.sampler_locations[0] = resources.shadow_diffuse_sampler;
            shadow.order = 0;
        }
    }

    auto* main_texture = animation->main.texture.get();
    if (!main_texture) return;
    const bool use_player = animation->player_color.usable() &&
        appearance->player_color_format == PlayerColorFormat::R8SubcolorAlphaBinary;
    if (use_player && !animation->player_color.texture.get()) return;
    main.valid = true;
    main.key.texture_count = use_player ? 3 : 1;
    main.key.textures[0] = main_texture->get_id();
    main.key.shader = static_cast<unsigned int>(use_player ? player_program : sprite_program);
    main.key.layers = layers;
    main.program = use_player ? &player_program : &sprite_program;
    main.diffuse_layer = &animation->main;
    main.sampler_locations[0] = use_player
        ? resources.player_diffuse_sampler : resources.sprite_diffuse_sampler;
    if (use_player) {
        main.key.textures[1] = animation->player_color.texture.get()->get_id();
        main.key.textures[2] = resources.palette_texture->get_id();
        main.player_layer = &animation->player_color;
        main.palette_texture = &resources.palette_texture;
        main.sampler_locations[1] = resources.player_mask_sampler;
        main.sampler_locations[2] = resources.player_palette_sampler;
    }
    main.order = 1;
}

void update_instance_full(InstanceData& instance, const Aoe2UnitRender& render,
                          const GlobalTransform& global, Aoe2BatchSourceKind kind) {
    const bool shadow = kind == Aoe2BatchSourceKind::Shadow;
    const Frame& frame = shadow ? render.shadow_frame : render.main_frame;
    instance = {};
    instance.rect = frame.uv;
    instance.pad = frame.uv;
    instance.color = shadow ? glm::vec4(1.f, 1.f, 1.f, render.tint.a) : render.tint;
    instance.transform = frame_transform(global.world, frame);
    instance.mparam0.x = static_cast<float>(render.player_color);
    instance.mparam0.y = static_cast<float>(render.player_color_debug);
}

void update_instance_frame(InstanceData& instance, const Aoe2UnitRender& render,
                           const GlobalTransform& global, Aoe2BatchSourceKind kind) {
    const Frame& frame = kind == Aoe2BatchSourceKind::Shadow
        ? render.shadow_frame : render.main_frame;
    instance.rect = frame.uv;
    instance.pad = frame.uv;
    instance.transform = frame_transform(global.world, frame);
}

void update_instance_transform(InstanceData& instance, const Aoe2UnitRender& render,
                               const GlobalTransform& global, Aoe2BatchSourceKind kind) {
    const Frame& frame = kind == Aoe2BatchSourceKind::Shadow
        ? render.shadow_frame : render.main_frame;
    instance.transform = frame_transform(global.world, frame);
}

void update_instance_material(InstanceData& instance, const Aoe2UnitRender& render,
                              Aoe2BatchSourceKind kind) {
    instance.color = kind == Aoe2BatchSourceKind::Shadow
        ? glm::vec4(1.f, 1.f, 1.f, render.tint.a) : render.tint;
    instance.mparam0.x = static_cast<float>(render.player_color);
    instance.mparam0.y = static_cast<float>(render.player_color_debug);
}

void update_group_instance(BatchComponent& batch, std::uint32_t instance_index,
                           Aoe2BatchSlot& slot, Aoe2BatchSourceKind kind,
                           const Aoe2UnitRender& render, const GlobalTransform& global,
                           std::uint8_t dirty_mask,
                           Aoe2PerformanceDiagnostics& performance) {
    auto& instance = batch.instances[instance_index];
    if (slot.needs_full_update) {
        update_instance_full(instance, render, global, kind);
        slot.needs_full_update = false;
        mark_batch_instance_dirty(batch, instance_index);
        ++performance.full_initialized_instances;
        ++performance.batch_rebuilt_instances;
        return;
    }
    const bool frame_dirty = (dirty_mask & FrameDirty) != 0;
    const bool transform_dirty = (dirty_mask & TransformDirty) != 0;
    const bool material_dirty = (dirty_mask & MaterialDirty) != 0;
    if (!frame_dirty && !transform_dirty && !material_dirty) {
        ++performance.unchanged_instances;
        return;
    }
    if (frame_dirty) update_instance_frame(instance, render, global, kind);
    else if (transform_dirty) update_instance_transform(instance, render, global, kind);
    if (material_dirty) update_instance_material(instance, render, kind);
    mark_batch_instance_dirty(batch, instance_index);
    if (frame_dirty) ++performance.frame_dirty_instances;
    if (transform_dirty) ++performance.transform_dirty_instances;
    if (material_dirty) ++performance.material_dirty_instances;
    ++performance.batch_rebuilt_instances;
}

void on_batch_source_destroy(entt::registry& reg, entt::entity entity) {
    auto* index = reg.ctx().find<Aoe2BatchIndex>();
    auto* membership = reg.try_get<Aoe2UnitBatchMembership>(entity);
    if (!index || !membership) return;
    detach_slot(reg, *index, entity, Aoe2BatchSourceKind::Main,
                membership->main, nullptr);
    detach_slot(reg, *index, entity, Aoe2BatchSourceKind::Shadow,
                membership->shadow, nullptr);
    // on_destroy also fires for direct component removal, not only entity
    // destruction. Invalidate both snapshots so re-adding a required source
    // component cannot be mistaken for an unchanged topology/instance state.
    membership->topology_initialized = false;
    membership->initialized = false;
    membership->pending_instance_dirty = 0;
}

} // namespace

void register_aoe2_batch_lifecycle(EcsWorld& world) {
    auto& index = world.resource_or_add<Aoe2BatchIndex>();
    if (index.lifecycle_connected) return;
    world.reg().on_destroy<Aoe2UnitRender>().connect<&on_batch_source_destroy>();
    world.reg().on_destroy<GlobalTransform>().connect<&on_batch_source_destroy>();
    world.reg().on_destroy<Aoe2UnitBatchMembership>().connect<&on_batch_source_destroy>();
    index.lifecycle_connected = true;
}

void aoe2_batch_system(EcsWorld& world) {
    const auto total_started = std::chrono::steady_clock::now();
    auto& performance = world.resource_or_add<Aoe2PerformanceDiagnostics>();
    performance.batch_total_ms = 0.0;
    performance.batch_group_ms = 0.0;
    performance.batch_rebuild_ms = 0.0;
    performance.membership_sync_ms = 0.0;
    performance.instance_update_ms = 0.0;
    performance.batch_units = 0;
    performance.batch_sources = 0;
    performance.batch_groups = 0;
    performance.batch_dirty_groups = 0;
    performance.batch_unchanged_groups = 0;
    performance.batch_rebuilt_instances = 0;
    performance.membership_attaches = 0;
    performance.membership_detaches = 0;
    performance.membership_migrations = 0;
    performance.group_creates = 0;
    performance.group_destroys = 0;
    performance.frame_dirty_instances = 0;
    performance.transform_dirty_instances = 0;
    performance.material_dirty_instances = 0;
    performance.full_initialized_instances = 0;
    performance.unchanged_instances = 0;

    auto& resources = world.resource_or_add<Aoe2RenderResources>();
    Program* sprite_program = resources.sprite_shader.get();
    Program* player_program = resources.player_color_shader.get();
    Program* shadow_program = resources.shadow_shader.get();
    if (!sprite_program || !player_program || !shadow_program || !resources.palette_texture) {
        performance.batch_total_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - total_started).count();
        return;
    }
    refresh_sampler_locations(resources, *sprite_program, *player_program, *shadow_program);

    auto& reg = world.reg();
    auto& index = world.resource_or_add<Aoe2BatchIndex>();
    register_aoe2_batch_lifecycle(world);
    std::uint64_t batch_resource_key = BatchSignatureSeed;
    batch_resource_key = batch_signature_append(
        batch_resource_key, static_cast<unsigned int>(*sprite_program));
    batch_resource_key = batch_signature_append(
        batch_resource_key, static_cast<unsigned int>(*player_program));
    batch_resource_key = batch_signature_append(
        batch_resource_key, static_cast<unsigned int>(*shadow_program));
    batch_resource_key = batch_signature_append(
        batch_resource_key, resources.palette_texture->get_id());
    batch_resource_key = batch_signature_append(
        batch_resource_key, resources.palette_revision);

    // Pass 1: derive desired keys and synchronize only changed memberships.
    auto membership_view = reg.view<Aoe2UnitRender, GlobalTransform>();
    for (auto entity : membership_view) {
        ++performance.batch_units;
        const auto& render = membership_view.get<Aoe2UnitRender>(entity);
        const auto& global = membership_view.get<GlobalTransform>(entity);
        auto& membership = reg.get_or_emplace<Aoe2UnitBatchMembership>(entity);
        const auto* layer = reg.try_get<RenderLayer>(entity);
        const std::uint32_t layers = layer ? layer->mask : 0x1u;
        const auto* appearance = render.appearance.get();
        const bool topology_changed = !membership.topology_initialized ||
            membership.seen_appearance != appearance ||
            membership.seen_animation_slot != render.animation_slot ||
            membership.seen_batch_resource_key != batch_resource_key ||
            membership.seen_layers != layers ||
            membership.seen_visible != render.visible ||
            membership.seen_has_main != render.has_main ||
            membership.seen_has_shadow != render.has_shadow;
        if (topology_changed) {
            DesiredBatch main;
            DesiredBatch shadow;
            build_desired_batches(render, layers, resources, *sprite_program,
                                  *player_program, *shadow_program, main, shadow);
            synchronize_slot(reg, index, entity, main, membership.main, performance);
            synchronize_slot(reg, index, entity, shadow, membership.shadow, performance);
            membership.seen_appearance = appearance;
            membership.seen_animation_slot = render.animation_slot;
            membership.seen_batch_resource_key = batch_resource_key;
            membership.seen_layers = layers;
            membership.seen_visible = render.visible;
            membership.seen_has_main = render.has_main;
            membership.seen_has_shadow = render.has_shadow;
            membership.topology_initialized = true;
        }
        if (membership.main.valid()) ++performance.batch_sources;
        if (membership.shadow.valid()) ++performance.batch_sources;
        std::uint8_t dirty_mask = 0;
        if (membership.initialized && membership.seen_render_revision != render.revision)
            dirty_mask |= FrameDirty;
        if (membership.initialized && membership.seen_transform_version != global.version)
            dirty_mask |= TransformDirty;
        if (membership.initialized &&
            (membership.seen_player_color != render.player_color ||
             membership.seen_player_color_debug != render.player_color_debug ||
             membership.seen_tint != render.tint))
            dirty_mask |= MaterialDirty;
        membership.pending_instance_dirty = dirty_mask;
        membership.seen_render_revision = render.revision;
        membership.seen_transform_version = global.version;
        membership.seen_player_color = render.player_color;
        membership.seen_player_color_debug = render.player_color_debug;
        membership.seen_tint = render.tint;
        membership.initialized = true;
    }
    performance.membership_sync_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - total_started).count();
    performance.batch_group_ms = performance.membership_sync_ms;

    // Pass 2: write persistent instance arrays group-by-group for locality.
    const auto update_started = std::chrono::steady_clock::now();
    for (std::uint32_t group_id = 0; group_id < index.groups.size(); ++group_id) {
        auto& group = index.groups[group_id];
        if (!group.active || !reg.valid(group.batch_entity)) continue;
        auto& batch = reg.get<BatchComponent>(group.batch_entity);
        for (std::uint32_t instance_index = 0;
             instance_index < group.members.size(); ++instance_index) {
            const auto member = group.members[instance_index];
            const auto& render = reg.get<Aoe2UnitRender>(member.entity);
            const auto& global = reg.get<GlobalTransform>(member.entity);
            auto& membership = reg.get<Aoe2UnitBatchMembership>(member.entity);
            auto& slot = membership_slot(membership, member.kind);
            if (slot.group_id != group_id || slot.instance_index != instance_index) continue;
            update_group_instance(batch, instance_index, slot, member.kind,
                                  render, global, membership.pending_instance_dirty,
                                  performance);
        }
    }
    performance.instance_update_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - update_started).count();
    performance.batch_rebuild_ms = performance.instance_update_ms;

    for (auto& group : index.groups) {
        if (!group.active || !reg.valid(group.batch_entity)) continue;
        ++performance.batch_groups;
        auto& batch = reg.get<BatchComponent>(group.batch_entity);
        batch.used = true;
        batch.count = batch.instances.size();
        if (batch.dirty || !batch.dirty_ranges.empty()) ++performance.batch_dirty_groups;
        else ++performance.batch_unchanged_groups;
    }
    performance.batch_total_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - total_started).count();
}

void destroy_aoe2_batches(EcsWorld& world) {
    auto* index = world.try_resource<Aoe2BatchIndex>();
    if (!index) return;
    auto& reg = world.reg();
    if (index->lifecycle_connected) {
        reg.on_destroy<Aoe2UnitRender>().disconnect<&on_batch_source_destroy>();
        reg.on_destroy<GlobalTransform>().disconnect<&on_batch_source_destroy>();
        reg.on_destroy<Aoe2UnitBatchMembership>().disconnect<&on_batch_source_destroy>();
        index->lifecycle_connected = false;
    }
    for (auto entity : reg.view<Aoe2UnitBatchMembership>()) {
        auto& membership = reg.get<Aoe2UnitBatchMembership>(entity);
        membership.main.reset();
        membership.shadow.reset();
    }
    for (auto& group : index->groups) {
        if (group.active && reg.valid(group.batch_entity)) reg.destroy(group.batch_entity);
        group = {};
    }
    index->key_to_group.clear();
    index->groups.clear();
    index->free_group_ids.clear();
}

} // namespace gld::ecs::aoe2
