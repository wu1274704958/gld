#include <aoe2/Aoe2Systems.hpp>

#include <chrono>
#include <cstdint>
#include <cmath>
#include <utility>

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>
#include <ecs/Components.hpp>
#include <ecs/PerformanceMonitoring.hpp>
#include <ecs/systems/TransformSystem.hpp>
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

Aoe2WorldInstance world_instance(const glm::mat4& world) {
    return {world[0], world[1], world[3]};
}

std::uint32_t pack_rgba8(const glm::vec4& color) {
    auto channel = [](float value) {
        return static_cast<std::uint32_t>(std::lround(
            glm::clamp(value, 0.f, 1.f) * 255.f));
    };
    return channel(color.r) | (channel(color.g) << 8u) |
           (channel(color.b) << 16u) | (channel(color.a) << 24u);
}

Aoe2VisualInstance visual_instance(const Aoe2UnitRender& render,
                                   Aoe2BatchSourceKind kind) {
    const bool shadow = kind == Aoe2BatchSourceKind::Shadow;
    const Frame& frame = shadow ? render.shadow_frame : render.main_frame;
    Aoe2VisualInstance result;
    result.uv = frame.uv;
    result.size_and_foot = {
        static_cast<float>(frame.width), static_cast<float>(frame.height),
        frame.foot.x, frame.foot.y
    };
    const glm::vec4 color = shadow
        ? glm::vec4(1.f, 1.f, 1.f, render.tint.a) : render.tint;
    result.tint_rgba8 = pack_rgba8(color);
    result.player_debug_flags =
        static_cast<std::uint32_t>(glm::clamp(render.player_color, 1, 8)) |
        (static_cast<std::uint32_t>(glm::clamp(render.player_color_debug, 0, 2)) << 4u) |
        (shadow ? (1u << 8u) : 0u);
    return result;
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

void mark_stream_dirty(bool full_dirty, std::vector<BatchUploadRange>& ranges,
                       std::uint32_t index) {
    if (full_dirty) return;
    if (!ranges.empty()) {
        auto& last = ranges.back();
        const std::uint64_t end = static_cast<std::uint64_t>(last.first_instance) +
            last.instance_count;
        if (index >= last.first_instance && index <= end) {
            last.instance_count = static_cast<std::uint32_t>(
                std::max<std::uint64_t>(end, static_cast<std::uint64_t>(index) + 1u) -
                last.first_instance);
            return;
        }
    }
    ranges.push_back({index, 1u});
}

void mark_world_dirty(Aoe2BatchComponent& batch, std::uint32_t index) {
    mark_stream_dirty(batch.world_full_dirty, batch.world_dirty_ranges, index);
}

void mark_visual_dirty(Aoe2BatchComponent& batch, std::uint32_t index) {
    mark_stream_dirty(batch.visual_full_dirty, batch.visual_dirty_ranges, index);
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
    GLD_PERF_MONITOR(
        if (performance) ++performance->group_destroys;
        if (auto* diagnostics = reg.ctx().find<RenderDiagnostics>())
            ++diagnostics->batch_groups_destroyed;
    );
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

    auto& batch = reg.get<Aoe2BatchComponent>(group->batch_entity);
    const auto detached_group_id = slot.group_id;
    const std::size_t last = group->members.size() - 1;
    if (position != last) {
        const auto moved = group->members[last];
        group->members[position] = moved;
        batch.world_instances[position] = std::move(batch.world_instances[last]);
        batch.visual_instances[position] = std::move(batch.visual_instances[last]);
        if (reg.valid(moved.entity)) {
            if (auto* moved_membership = reg.try_get<Aoe2UnitBatchMembership>(moved.entity)) {
                auto& moved_slot = membership_slot(*moved_membership, moved.kind);
                moved_slot.group_id = slot.group_id;
                moved_slot.instance_index = static_cast<std::uint32_t>(position);
            }
        }
        mark_world_dirty(batch, static_cast<std::uint32_t>(position));
        mark_visual_dirty(batch, static_cast<std::uint32_t>(position));
    }
    group->members.pop_back();
    batch.world_instances.pop_back();
    batch.visual_instances.pop_back();
    slot.reset();
    GLD_PERF_MONITOR(if (performance) ++performance->membership_detaches);
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

    auto& batch = reg.emplace<Aoe2BatchComponent>(group.batch_entity);
    batch.key = desired.key;
    batch.layers = desired.key.layers;
    batch.program = desired.program;
    batch.order = desired.order;
    batch.sampler_locations = desired.sampler_locations;
    if (desired.diffuse_layer)
        batch.texture_refs[0] = desired.diffuse_layer->texture.shared();
    if (desired.player_layer)
        batch.texture_refs[1] = desired.player_layer->texture.shared();
    if (desired.palette_texture)
        batch.texture_refs[2] = *desired.palette_texture;
    index.key_to_group.emplace(desired.key, group_id);
    GLD_PERF_MONITOR(++performance.group_creates);
    return group_id;
}

void attach_slot(entt::registry& reg, Aoe2BatchIndex& index, entt::entity owner,
                 const DesiredBatch& desired, Aoe2BatchSlot& slot,
                 Aoe2PerformanceDiagnostics& performance) {
    const auto group_id = ensure_group(reg, index, desired, performance);
    auto& group = index.groups[group_id];
    auto& batch = reg.get<Aoe2BatchComponent>(group.batch_entity);
    const auto position = static_cast<std::uint32_t>(group.members.size());
    group.members.push_back({owner, desired.kind});
    batch.world_instances.emplace_back();
    batch.visual_instances.emplace_back();
    mark_world_dirty(batch, position);
    mark_visual_dirty(batch, position);
    slot.group_id = group_id;
    slot.instance_index = position;
    slot.needs_full_update = true;
    GLD_PERF_MONITOR(++performance.membership_attaches);
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
        GLD_PERF_MONITOR(++performance.membership_migrations);
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

void update_group_instance(Aoe2BatchComponent& batch, std::uint32_t instance_index,
                           Aoe2BatchSlot& slot, Aoe2BatchSourceKind kind,
                           const Aoe2UnitRender& render, const GlobalTransform& global,
                           std::uint8_t dirty_mask,
                           Aoe2PerformanceDiagnostics& performance) {
    if (slot.needs_full_update) {
        batch.world_instances[instance_index] = world_instance(global.world);
        batch.visual_instances[instance_index] = visual_instance(render, kind);
        slot.needs_full_update = false;
        mark_world_dirty(batch, instance_index);
        mark_visual_dirty(batch, instance_index);
        GLD_PERF_MONITOR(
            ++performance.full_initialized_instances;
            ++performance.batch_rebuilt_instances;
        );
        return;
    }
    const bool frame_dirty = (dirty_mask & FrameDirty) != 0;
    const bool transform_dirty = (dirty_mask & TransformDirty) != 0;
    const bool material_dirty = (dirty_mask & MaterialDirty) != 0;
    if (!frame_dirty && !transform_dirty && !material_dirty) {
        GLD_PERF_MONITOR(++performance.unchanged_instances);
        return;
    }
    if (transform_dirty) {
        batch.world_instances[instance_index] = world_instance(global.world);
        mark_world_dirty(batch, instance_index);
    }
    if (frame_dirty || material_dirty) {
        batch.visual_instances[instance_index] = visual_instance(render, kind);
        mark_visual_dirty(batch, instance_index);
    }
    GLD_PERF_MONITOR(
        if (frame_dirty) ++performance.frame_dirty_instances;
        if (transform_dirty) ++performance.transform_dirty_instances;
        if (material_dirty) ++performance.material_dirty_instances;
        ++performance.batch_rebuilt_instances;
    );
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

void enqueue_aoe2_dirty(entt::registry& registry, entt::entity entity,
                        Aoe2RenderDirty dirty) {
    auto& queue = registry.ctx().get<Aoe2DirtyQueue>();
    queue.enqueue(entity, dirty);
}

void on_aoe2_render_construct(entt::registry& registry, entt::entity entity) {
    enqueue_aoe2_dirty(registry, entity, Aoe2RenderDirty::Full);
}

void on_aoe2_render_update(entt::registry& registry, entt::entity entity) {
    enqueue_aoe2_dirty(registry, entity, Aoe2RenderDirty::Full);
}

void on_aoe2_layer_change(entt::registry& registry, entt::entity entity) {
    if (registry.all_of<Aoe2UnitRender>(entity))
        enqueue_aoe2_dirty(registry, entity, Aoe2RenderDirty::Topology);
}

} // namespace

void register_aoe2_batch_lifecycle(EcsWorld& world) {
    auto& index = world.resource_or_add<Aoe2BatchIndex>();
    world.resource_or_add<Aoe2DirtyQueue>();
    if (index.lifecycle_connected) return;
    world.reg().on_destroy<Aoe2UnitRender>().connect<&on_batch_source_destroy>();
    world.reg().on_destroy<GlobalTransform>().connect<&on_batch_source_destroy>();
    world.reg().on_destroy<Aoe2UnitBatchMembership>().connect<&on_batch_source_destroy>();
    world.reg().on_construct<Aoe2UnitRender>().connect<&on_aoe2_render_construct>();
    world.reg().on_update<Aoe2UnitRender>().connect<&on_aoe2_render_update>();
    world.reg().on_construct<RenderLayer>().connect<&on_aoe2_layer_change>();
    world.reg().on_update<RenderLayer>().connect<&on_aoe2_layer_change>();
    world.reg().on_destroy<RenderLayer>().connect<&on_aoe2_layer_change>();
    index.lifecycle_connected = true;
}

void aoe2_batch_system(EcsWorld& world) {
    GLD_PERF_TIME_POINT(total_started);
    auto& performance = world.resource_or_add<Aoe2PerformanceDiagnostics>();
    GLD_PERF_MONITOR(
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
    performance.dirty_enqueued = 0;
    performance.dirty_deduplicated = 0;
    performance.dirty_audit_violations = 0;
    performance.dirty_dense_mode = false;
    performance.world_upload_bytes = 0;
    performance.visual_upload_bytes = 0;
    performance.world_uploads = 0;
    performance.visual_uploads = 0;
    performance.upload_ranges = 0;
    performance.render_prepare_ms = 0.0;
    performance.render_upload_ms = 0.0;
    performance.render_submit_ms = 0.0;
    performance.render_gpu_query_skips = 0;
    );

    auto& resources = world.resource_or_add<Aoe2RenderResources>();
    Program* sprite_program = resources.sprite_shader.get();
    Program* player_program = resources.player_color_shader.get();
    Program* shadow_program = resources.shadow_shader.get();
    if (!sprite_program || !player_program || !shadow_program || !resources.palette_texture) {
        GLD_PERF_MONITOR(
            performance.batch_total_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - total_started).count();
        );
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

    auto& dirty_queue = world.resource_or_add<Aoe2DirtyQueue>();
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING) && !defined(NDEBUG)
    if (const auto* time = world.try_resource<Time>(); time && time->frame % 120u == 0u) {
        for (auto entity : reg.view<Aoe2UnitRender, GlobalTransform,
                                    Aoe2UnitBatchMembership>()) {
            const auto& render = reg.get<Aoe2UnitRender>(entity);
            const auto& global = reg.get<GlobalTransform>(entity);
            const auto& membership = reg.get<Aoe2UnitBatchMembership>(entity);
            if (!membership.initialized) continue;
            const auto* layer = reg.try_get<RenderLayer>(entity);
            const std::uint32_t layers = layer ? layer->mask : 0x1u;
            const bool mismatch = membership.seen_appearance != render.appearance.get() ||
                membership.seen_animation_slot != render.animation_slot ||
                membership.seen_layers != layers ||
                membership.seen_visible != render.visible ||
                membership.seen_has_main != render.has_main ||
                membership.seen_has_shadow != render.has_shadow ||
                membership.seen_render_revision != render.revision ||
                membership.seen_transform_version != global.version ||
                membership.seen_player_color != render.player_color ||
                membership.seen_player_color_debug != render.player_color_debug ||
                membership.seen_tint != render.tint;
            if (mismatch && !dirty_queue.contains(entity)) {
                mark_aoe2_render_dirty(world, entity, Aoe2RenderDirty::Full);
                ++performance.dirty_audit_violations;
            }
        }
    }
#endif
    if (const auto* transform_changes = world.try_resource<TransformChanges>())
        for (auto entity : transform_changes->entities)
            if (reg.all_of<Aoe2UnitRender>(entity))
                mark_aoe2_render_dirty(world, entity, Aoe2RenderDirty::Transform);
    GLD_PERF_MONITOR(
        performance.dirty_enqueued = dirty_queue.enqueued;
        performance.dirty_deduplicated = dirty_queue.deduplicated;
        dirty_queue.enqueued = 0;
        dirty_queue.deduplicated = 0;
    );
    dirty_queue.begin_processing();

    auto source_view = reg.view<Aoe2UnitRender, GlobalTransform>();
    const auto source_count = source_view.size_hint();
    const bool dirty_dense_mode = source_count != 0u &&
        dirty_queue.processing.size() * 2u >= source_count;
    GLD_PERF_MONITOR(performance.dirty_dense_mode = dirty_dense_mode);
    auto for_each_dirty = [&](auto&& function) {
        if (dirty_dense_mode) {
            for (const auto entity : source_view) {
                const auto mask = dirty_queue.processing_dirty(entity);
                if (mask != Aoe2RenderDirty::None)
                    function(Aoe2DirtyEntry{entity, mask});
            }
            return;
        }
        for (const auto& entry : dirty_queue.processing) {
            const auto mask = dirty_queue.processing_dirty(entry.entity);
            if (mask != Aoe2RenderDirty::None)
                function(Aoe2DirtyEntry{entry.entity, mask});
        }
    };

    // Pass 1: synchronize topology and resolve precise stream dirty masks only
    // for entities queued by animation/gameplay/Transform lifecycle changes.
    for_each_dirty([&](const Aoe2DirtyEntry& dirty) {
        const auto entity = dirty.entity;
        if (!reg.valid(entity) || !reg.all_of<Aoe2UnitRender, GlobalTransform>(entity))
            return;
        GLD_PERF_MONITOR(++performance.batch_units);
        const auto& render = reg.get<Aoe2UnitRender>(entity);
        const auto& global = reg.get<GlobalTransform>(entity);
        auto& membership = reg.get_or_emplace<Aoe2UnitBatchMembership>(entity);
        const auto* layer = reg.try_get<RenderLayer>(entity);
        const std::uint32_t layers = layer ? layer->mask : 0x1u;
        const auto* appearance = render.appearance.get();
        const auto requested = static_cast<std::uint8_t>(dirty.mask);
        const bool topology_changed = (requested & static_cast<std::uint8_t>(
                Aoe2RenderDirty::Topology)) != 0 || !membership.topology_initialized ||
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
        GLD_PERF_MONITOR(
            if (membership.main.valid()) ++performance.batch_sources;
            if (membership.shadow.valid()) ++performance.batch_sources;
        );
        std::uint8_t dirty_mask = 0;
        if ((requested & static_cast<std::uint8_t>(Aoe2RenderDirty::Frame)) != 0)
            dirty_mask |= FrameDirty;
        if ((requested & static_cast<std::uint8_t>(Aoe2RenderDirty::Transform)) != 0)
            dirty_mask |= TransformDirty;
        if ((requested & static_cast<std::uint8_t>(Aoe2RenderDirty::Material)) != 0)
            dirty_mask |= MaterialDirty;
        membership.pending_instance_dirty = dirty_mask;
        membership.seen_render_revision = render.revision;
        membership.seen_transform_version = global.version;
        membership.seen_player_color = render.player_color;
        membership.seen_player_color_debug = render.player_color_debug;
        membership.seen_tint = render.tint;
        membership.initialized = true;
    });
    GLD_PERF_MONITOR(
        performance.membership_sync_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - total_started).count();
        performance.batch_group_ms = performance.membership_sync_ms;
    );

    // Pass 2: update both persistent slots directly. Compact streams make these
    // writes small, while avoiding a second scan of every member in every group.
    GLD_PERF_TIME_POINT(update_started);
    for_each_dirty([&](const Aoe2DirtyEntry& dirty) {
        const auto entity = dirty.entity;
        if (!reg.valid(entity) ||
            !reg.all_of<Aoe2UnitRender, GlobalTransform, Aoe2UnitBatchMembership>(entity))
            return;
        const auto& render = reg.get<Aoe2UnitRender>(entity);
        const auto& global = reg.get<GlobalTransform>(entity);
        auto& membership = reg.get<Aoe2UnitBatchMembership>(entity);
        auto update_slot = [&](Aoe2BatchSlot& slot, Aoe2BatchSourceKind kind) {
            auto* group = active_group(index, slot.group_id);
            if (!slot.valid() || !group || !reg.valid(group->batch_entity)) return;
            auto& batch = reg.get<Aoe2BatchComponent>(group->batch_entity);
            if (slot.instance_index >= batch.world_instances.size()) return;
            update_group_instance(batch, slot.instance_index, slot, kind, render, global,
                                  membership.pending_instance_dirty, performance);
        };
        update_slot(membership.main, Aoe2BatchSourceKind::Main);
        update_slot(membership.shadow, Aoe2BatchSourceKind::Shadow);
    });
    dirty_queue.end_processing();
    GLD_PERF_MONITOR(
        performance.instance_update_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - update_started).count();
        performance.batch_rebuild_ms = performance.instance_update_ms;
    );

    GLD_PERF_MONITOR(
        for (auto& group : index.groups) {
            if (!group.active || !reg.valid(group.batch_entity)) continue;
            ++performance.batch_groups;
            auto& batch = reg.get<Aoe2BatchComponent>(group.batch_entity);
            if (batch.world_full_dirty || batch.visual_full_dirty ||
                !batch.world_dirty_ranges.empty() || !batch.visual_dirty_ranges.empty())
                ++performance.batch_dirty_groups;
            else ++performance.batch_unchanged_groups;
        }
    );
    GLD_PERF_MONITOR(
        performance.batch_total_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - total_started).count();
    );
}

namespace {

void destroy_aoe2_batch_gpu(Aoe2BatchComponent& batch) {
    if (batch.visual_vbo != 0) glDeleteBuffers(1, &batch.visual_vbo);
    if (batch.world_vbo != 0) glDeleteBuffers(1, &batch.world_vbo);
    if (batch.vao != 0) glDeleteVertexArrays(1, &batch.vao);
    batch.visual_vbo = 0;
    batch.world_vbo = 0;
    batch.vao = 0;
    batch.visual_gpu_cap = 0;
    batch.world_gpu_cap = 0;
}

void ensure_aoe2_quad(Aoe2RenderResources& resources) {
    if (resources.quad_ready) return;
    static constexpr float vertices[] = {
         0.5f,  0.5f, 0.f,
        -0.5f,  0.5f, 0.f,
        -0.5f, -0.5f, 0.f,
         0.5f, -0.5f, 0.f,
    };
    static constexpr unsigned int indices[] = {0, 1, 2, 0, 2, 3};
    glGenBuffers(1, &resources.quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, resources.quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glGenBuffers(1, &resources.quad_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resources.quad_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    resources.quad_ready = true;
}

void setup_aoe2_vao(Aoe2RenderResources& resources, Aoe2BatchComponent& batch) {
    glGenVertexArrays(1, &batch.vao);
    glGenBuffers(1, &batch.world_vbo);
    glGenBuffers(1, &batch.visual_vbo);
    glBindVertexArray(batch.vao);
    glBindBuffer(GL_ARRAY_BUFFER, resources.quad_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resources.quad_ebo);

    glBindBuffer(GL_ARRAY_BUFFER, batch.world_vbo);
    const GLsizei world_stride = sizeof(Aoe2WorldInstance);
    auto world_attribute = [&](GLuint location, std::size_t offset) {
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, world_stride,
                              reinterpret_cast<void*>(offset));
        glVertexAttribDivisor(location, 1);
    };
    world_attribute(1, offsetof(Aoe2WorldInstance, axis_x));
    world_attribute(2, offsetof(Aoe2WorldInstance, axis_y));
    world_attribute(3, offsetof(Aoe2WorldInstance, origin));

    glBindBuffer(GL_ARRAY_BUFFER, batch.visual_vbo);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Aoe2VisualInstance),
                          reinterpret_cast<void*>(offsetof(Aoe2VisualInstance, uv)));
    glVertexAttribDivisor(4, 1);
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Aoe2VisualInstance),
                          reinterpret_cast<void*>(offsetof(Aoe2VisualInstance, size_and_foot)));
    glVertexAttribDivisor(5, 1);
    glEnableVertexAttribArray(6);
    glVertexAttribIPointer(6, 2, GL_UNSIGNED_INT, sizeof(Aoe2VisualInstance),
                           reinterpret_cast<void*>(offsetof(Aoe2VisualInstance, tint_rgba8)));
    glVertexAttribDivisor(6, 1);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

struct Aoe2UploadResult {
    std::size_t bytes = 0;
    std::uint32_t calls = 0;
    bool full = false;
};

template<class Instance>
Aoe2UploadResult upload_aoe2_stream(unsigned int vbo,
                                    const std::vector<Instance>& instances,
                                    std::vector<BatchUploadRange>& dirty_ranges,
                                    bool& full_dirty, std::size_t& gpu_capacity) {
    Aoe2UploadResult result;
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    const bool grow = instances.size() > gpu_capacity;
    auto plan = plan_batch_upload(dirty_ranges, instances.size(), full_dirty || grow,
                                  BatchUploadPolicy{8, 75});
    if (grow) {
        gpu_capacity = next_batch_gpu_capacity(instances.size());
        glBufferData(GL_ARRAY_BUFFER, gpu_capacity * sizeof(Instance), nullptr,
                     GL_DYNAMIC_DRAW);
    }
    if (plan.full) {
        const auto bytes = instances.size() * sizeof(Instance);
        glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, instances.data());
        result.bytes = bytes;
        result.calls = 1;
        result.full = true;
    } else {
        for (const auto& range : plan.ranges) {
            const auto offset = static_cast<std::size_t>(range.first_instance) *
                sizeof(Instance);
            const auto bytes = static_cast<std::size_t>(range.instance_count) *
                sizeof(Instance);
            glBufferSubData(GL_ARRAY_BUFFER, offset, bytes,
                            instances.data() + range.first_instance);
            result.bytes += bytes;
            ++result.calls;
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    full_dirty = false;
    dirty_ranges.clear();
    return result;
}

void record_aoe2_upload(RenderDiagnostics& render, Aoe2PerformanceDiagnostics& aoe,
                        const Aoe2UploadResult& upload, bool world_stream) {
    render.batch_upload_bytes += upload.bytes;
    render.batch_upload_ranges += upload.calls;
    ++render.batch_uploads;
    if (upload.full) {
        ++render.batch_full_uploads;
        render.batch_full_upload_bytes += upload.bytes;
    } else {
        ++render.batch_partial_uploads;
        render.batch_partial_upload_bytes += upload.bytes;
    }
    aoe.upload_ranges += upload.calls;
    if (world_stream) {
        aoe.world_upload_bytes += upload.bytes;
        ++aoe.world_uploads;
    } else {
        aoe.visual_upload_bytes += upload.bytes;
        ++aoe.visual_uploads;
    }
}

#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
int begin_aoe2_gpu_timer(Aoe2RenderResources& resources,
                         Aoe2PerformanceDiagnostics& diagnostics) {
    if (resources.gpu_time_queries[0] == 0)
        glGenQueries(static_cast<GLsizei>(resources.gpu_time_queries.size()),
                     resources.gpu_time_queries.data());

    for (std::size_t i = 0; i < resources.gpu_time_queries.size(); ++i) {
        if (!resources.gpu_time_pending[i]) continue;
        GLint available = GL_FALSE;
        glGetQueryObjectiv(resources.gpu_time_queries[i], GL_QUERY_RESULT_AVAILABLE,
                           &available);
        if (available == GL_TRUE) {
            GLuint64 nanoseconds = 0;
            glGetQueryObjectui64v(resources.gpu_time_queries[i], GL_QUERY_RESULT,
                                  &nanoseconds);
            diagnostics.render_gpu_ms = static_cast<double>(nanoseconds) / 1'000'000.0;
            resources.gpu_time_pending[i] = false;
        }
    }

    for (std::size_t offset = 0; offset < resources.gpu_time_queries.size(); ++offset) {
        const auto slot = static_cast<std::uint32_t>(
            (resources.gpu_time_write + offset) % resources.gpu_time_queries.size());
        if (resources.gpu_time_pending[slot]) continue;
        glBeginQuery(GL_TIME_ELAPSED, resources.gpu_time_queries[slot]);
        resources.gpu_time_write = static_cast<std::uint32_t>(
            (slot + 1u) % resources.gpu_time_queries.size());
        return static_cast<int>(slot);
    }
    ++diagnostics.render_gpu_query_skips;
    return -1;
}

void end_aoe2_gpu_timer(Aoe2RenderResources& resources, int slot) {
    if (slot < 0) return;
    glEndQuery(GL_TIME_ELAPSED);
    resources.gpu_time_pending[static_cast<std::size_t>(slot)] = true;
}
#endif

} // namespace

Aoe2BatchComponent::~Aoe2BatchComponent() {
    destroy_aoe2_batch_gpu(*this);
}

Aoe2BatchComponent::Aoe2BatchComponent(Aoe2BatchComponent&& other) noexcept {
    *this = std::move(other);
}

Aoe2BatchComponent& Aoe2BatchComponent::operator=(Aoe2BatchComponent&& other) noexcept {
    if (this == &other) return *this;
    destroy_aoe2_batch_gpu(*this);
    key = other.key;
    layers = other.layers;
    program = other.program;
    texture_refs = std::move(other.texture_refs);
    sampler_locations = other.sampler_locations;
    order = other.order;
    world_instances = std::move(other.world_instances);
    visual_instances = std::move(other.visual_instances);
    world_dirty_ranges = std::move(other.world_dirty_ranges);
    visual_dirty_ranges = std::move(other.visual_dirty_ranges);
    world_full_dirty = other.world_full_dirty;
    visual_full_dirty = other.visual_full_dirty;
    vao = std::exchange(other.vao, 0);
    world_vbo = std::exchange(other.world_vbo, 0);
    visual_vbo = std::exchange(other.visual_vbo, 0);
    world_gpu_cap = std::exchange(other.world_gpu_cap, 0);
    visual_gpu_cap = std::exchange(other.visual_gpu_cap, 0);
    return *this;
}

RegisteredRenderPass make_aoe2_unit_pass() {
    return {Aoe2UnitPassId, {}};
}

void render_aoe2_unit_pass(RenderPassContext& context,
                           const RegisteredRenderPass&,
                           const ResolvedRenderPassState& pass_state) {
    GLD_PERF_TIME_POINT(prepare_started);
    auto& world = context.world;
    auto& registry = world.reg();
    auto& resources = world.resource_or_add<Aoe2RenderResources>();
    auto& render_diagnostics = world.resource_or_add<RenderDiagnostics>();
    auto& aoe_diagnostics = world.resource_or_add<Aoe2PerformanceDiagnostics>();
    ensure_aoe2_quad(resources);

    std::vector<entt::entity> ordered;
    for (auto entity : registry.view<Aoe2BatchComponent>()) ordered.push_back(entity);
    std::sort(ordered.begin(), ordered.end(), [&](entt::entity lhs, entt::entity rhs) {
        const auto& a = registry.get<Aoe2BatchComponent>(lhs);
        const auto& b = registry.get<Aoe2BatchComponent>(rhs);
        if (a.order != b.order) return a.order < b.order;
        return static_cast<std::uint32_t>(lhs) < static_cast<std::uint32_t>(rhs);
    });
    GLD_PERF_MONITOR(
        aoe_diagnostics.render_prepare_ms += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - prepare_started).count();
    );

#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const int gpu_timer_slot = begin_aoe2_gpu_timer(resources, aoe_diagnostics);
#endif
    GLD_PERF_TIME_POINT(submit_started);
    double upload_ms = 0.0;
    for (auto entity : ordered) {
        auto& batch = registry.get<Aoe2BatchComponent>(entity);
        if (batch.world_instances.empty() ||
            (batch.layers & context.camera.layers) == 0 || !batch.program) continue;
        context.state_cache.depth(resolve_batch_state(batch.key.depth_test,
                                                       pass_state.depth_test));
        context.state_cache.depth_write(resolve_batch_state(batch.key.depth_write,
                                                             pass_state.depth_write));
        batch.program->use();
        if (batch.program->uniform_id("uViewProj") < 0)
            batch.program->locat_uniforms("uViewProj");
        const auto view_projection = context.camera.projection * context.camera.view;
        const int view_projection_location = batch.program->uniform_id("uViewProj");
        if (view_projection_location >= 0)
            glUniformMatrix4fv(view_projection_location, 1, GL_FALSE,
                               glm::value_ptr(view_projection));

        if (batch.key.texture_count == 1 && batch.key.textures[0] != 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, batch.key.textures[0]);
            if (batch.sampler_locations[0] >= 0)
                glUniform1i(batch.sampler_locations[0], 0);
        } else {
            for (std::uint8_t slot = 0; slot < batch.key.texture_count; ++slot) {
                glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + slot));
                glBindTexture(GL_TEXTURE_2D, batch.key.textures[slot]);
                if (batch.sampler_locations[slot] >= 0)
                    glUniform1i(batch.sampler_locations[slot], slot);
            }
            glActiveTexture(GL_TEXTURE0);
        }

        if (batch.vao == 0) setup_aoe2_vao(resources, batch);
        const bool upload_world = batch.world_full_dirty ||
            !batch.world_dirty_ranges.empty() ||
            batch.world_instances.size() > batch.world_gpu_cap;
        const bool upload_visual = batch.visual_full_dirty ||
            !batch.visual_dirty_ranges.empty() ||
            batch.visual_instances.size() > batch.visual_gpu_cap;
        if (upload_world || upload_visual) {
            GLD_PERF_TIME_POINT(upload_started);
            if (upload_world) {
                const auto uploaded = upload_aoe2_stream(
                    batch.world_vbo, batch.world_instances, batch.world_dirty_ranges,
                    batch.world_full_dirty, batch.world_gpu_cap);
                GLD_PERF_MONITOR(
                    record_aoe2_upload(
                        render_diagnostics, aoe_diagnostics, uploaded, true);
                );
            }
            if (upload_visual) {
                const auto uploaded = upload_aoe2_stream(
                    batch.visual_vbo, batch.visual_instances, batch.visual_dirty_ranges,
                    batch.visual_full_dirty, batch.visual_gpu_cap);
                GLD_PERF_MONITOR(
                    record_aoe2_upload(
                        render_diagnostics, aoe_diagnostics, uploaded, false);
                );
            }
            GLD_PERF_MONITOR(
                const auto elapsed = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - upload_started).count();
                upload_ms += elapsed;
                aoe_diagnostics.render_upload_ms += elapsed;
                render_diagnostics.batch_upload_ms += elapsed;
            );
        }

        glBindVertexArray(batch.vao);
        glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr,
                                static_cast<GLsizei>(batch.world_instances.size()));
        glBindVertexArray(0);
        GLD_PERF_MONITOR(
            ++render_diagnostics.batch_draws;
            render_diagnostics.batch_instances += static_cast<std::uint32_t>(
                batch.world_instances.size());
        );
    }
    GLD_PERF_MONITOR(
        const auto submit_total = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - submit_started).count();
        aoe_diagnostics.render_submit_ms += std::max(0.0, submit_total - upload_ms);
        render_diagnostics.batch_submit_ms += std::max(0.0, submit_total - upload_ms);
        render_diagnostics.batch_groups = std::max(render_diagnostics.batch_groups,
            static_cast<std::uint32_t>(ordered.size()));
    );
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    end_aoe2_gpu_timer(resources, gpu_timer_slot);
#endif
}

void destroy_aoe2_batches(EcsWorld& world) {
    auto* index = world.try_resource<Aoe2BatchIndex>();
    if (!index) return;
    auto& reg = world.reg();
    if (index->lifecycle_connected) {
        reg.on_destroy<Aoe2UnitRender>().disconnect<&on_batch_source_destroy>();
        reg.on_destroy<GlobalTransform>().disconnect<&on_batch_source_destroy>();
        reg.on_destroy<Aoe2UnitBatchMembership>().disconnect<&on_batch_source_destroy>();
        reg.on_construct<Aoe2UnitRender>().disconnect<&on_aoe2_render_construct>();
        reg.on_update<Aoe2UnitRender>().disconnect<&on_aoe2_render_update>();
        reg.on_construct<RenderLayer>().disconnect<&on_aoe2_layer_change>();
        reg.on_update<RenderLayer>().disconnect<&on_aoe2_layer_change>();
        reg.on_destroy<RenderLayer>().disconnect<&on_aoe2_layer_change>();
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
    if (auto* resources = world.try_resource<Aoe2RenderResources>()) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        if (resources->gpu_time_queries[0] != 0)
            glDeleteQueries(static_cast<GLsizei>(resources->gpu_time_queries.size()),
                            resources->gpu_time_queries.data());
        resources->gpu_time_queries = {};
        resources->gpu_time_pending = {};
        resources->gpu_time_write = 0;
#endif
        if (resources->quad_ebo != 0) glDeleteBuffers(1, &resources->quad_ebo);
        if (resources->quad_vbo != 0) glDeleteBuffers(1, &resources->quad_vbo);
        resources->quad_ebo = 0;
        resources->quad_vbo = 0;
        resources->quad_ready = false;
    }
    unregister_render_pass(world, Aoe2UnitPassId);
}

} // namespace gld::ecs::aoe2
