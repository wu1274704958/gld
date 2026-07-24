#include <cassert>
#include <array>
#include <chrono>
#include <cmath>

#include <ecs/systems/TransformSystem.hpp>
#include <ecs/render/Batch.hpp>
#include <ecs/assets/Loader.hpp>
#include <aoe2/Aoe2Assets.hpp>
#include <aoe2/Aoe2Systems.hpp>

using namespace gld::ecs;
using namespace gld::ecs::aoe2;

int main() {
    static_assert(animation_slot_value(AnimationSlot::Invalid) == 0);
    static_assert(animation_slot_value(AnimationSlot::WalkA) == 1);
    static_assert(animation_slot_value(AnimationSlot::WalkB) == 2);
    static_assert(animation_slot_value(AnimationSlot::WalkC) == 3);
    static_assert(animation_slot_value(AnimationSlot::IdleA) == 4);
    static_assert(animation_slot_value(AnimationSlot::IdleB) == 5);
    static_assert(animation_slot_value(AnimationSlot::IdleC) == 6);
    static_assert(animation_slot_value(AnimationSlot::AttackA) == 7);
    static_assert(animation_slot_value(AnimationSlot::AttackB) == 8);
    static_assert(animation_slot_value(AnimationSlot::AttackC) == 9);
    static_assert(animation_slot_value(AnimationSlot::DeathA) == 10);
    static_assert(animation_slot_value(AnimationSlot::DecayA) == 11);
    static_assert(animation_slot_value(AnimationSlot::ExtensionBegin) == 0x100);
    static_assert(DefaultAoe2AnimationAbi::find("walkA") == AnimationSlot::WalkA);
    static_assert(DefaultAoe2AnimationAbi::find("idleB") == AnimationSlot::IdleB);
    static_assert(DefaultAoe2AnimationAbi::find("runA") == AnimationSlot::Invalid);
    static_assert(DefaultAoe2AnimationAbi::name(AnimationSlot::AttackA) == "attackA");

    Aoe2UnitAppearance appearance;
    auto add_animation = [&](std::string name) {
        Animation animation;
        animation.name = std::move(name);
        appearance.animations.push_back(std::move(animation));
    };
    add_animation("taskA");
    add_animation("idleB");
    add_animation("carryidleA");
    add_animation("walkA");
    add_animation("runA");
    appearance.build_animation_slots();
    assert(appearance.find_animation_slot("walkA") == AnimationSlot::WalkA);
    assert(appearance.find_animation_slot("idleB") == AnimationSlot::IdleB);
    assert(appearance.extension_animation_count() == 3);
    assert(appearance.extension_animation_names()[0] == "carryidleA");
    assert(appearance.extension_animation_names()[1] == "runA");
    assert(appearance.extension_animation_names()[2] == "taskA");
    const auto carry_slot = appearance.find_animation_slot("carryidleA");
    const auto run_slot = appearance.find_animation_slot("runA");
    const auto task_slot = appearance.find_animation_slot("taskA");
    assert(animation_slot_value(carry_slot) == 0x100);
    assert(animation_slot_value(run_slot) == 0x101);
    assert(animation_slot_value(task_slot) == 0x102);
    assert(appearance.is_extension_animation(run_slot));
    assert(appearance.animation_name(run_slot) == "runA");
    assert(appearance.animation_at(AnimationSlot::WalkA)->name == "walkA");
    assert(appearance.animation_at(static_cast<AnimationSlot>(12)) == nullptr);
    assert(appearance.find_animation("taskA") == appearance.animation_at(task_slot));

    Aoe2UnitAppearance reordered;
    for (const auto* name : {"runA", "walkA", "taskA", "carryidleA", "idleB"}) {
        Animation animation;
        animation.name = name;
        reordered.animations.push_back(std::move(animation));
    }
    reordered.build_animation_slots();
    assert(reordered.find_animation_slot("carryidleA") == carry_slot);
    assert(reordered.find_animation_slot("runA") == run_slot);
    assert(reordered.find_animation_slot("taskA") == task_slot);

    Time time;
    TimeClock clock;
    TimeSettings settings;
    settings.max_delta = 0.1f;
    const TimeClock::Clock::time_point start{};

    update_time(time, clock, settings, start);
    assert(time.frame == 1);
    assert(time.dt == 0.f);
    assert(time.raw_dt == 0.f);

    update_time(time, clock, settings, start + std::chrono::milliseconds(16));
    assert(std::abs(time.raw_dt - 0.016f) < 0.0001f);
    assert(std::abs(time.dt - 0.016f) < 0.0001f);

    update_time(time, clock, settings, start + std::chrono::milliseconds(266));
    assert(std::abs(time.raw_dt - 0.250f) < 0.0001f);
    assert(std::abs(time.dt - 0.100f) < 0.0001f);

    BatchKey one;
    one.texture_count = 1;
    one.textures[0] = 11;
    one.shader = 3;
    BatchKey same = one;
    assert(one == same);
    same.texture_count = 3;
    same.textures[1] = 12;
    same.textures[2] = 13;
    assert(!(one == same));
    assert(BatchKeyHash{}(one) != BatchKeyHash{}(same));

    BatchKey shadow = one;
    shadow.depth_write = BatchStateOverride::Disabled;
    assert(!(one == shadow));
    assert(BatchKeyHash{}(one) != BatchKeyHash{}(shadow));
    assert(resolve_batch_state(BatchStateOverride::Inherit, true));
    assert(!resolve_batch_state(BatchStateOverride::Inherit, false));
    assert(resolve_batch_state(BatchStateOverride::Enabled, false));
    assert(!resolve_batch_state(BatchStateOverride::Disabled, true));

    assert(next_batch_gpu_capacity(0) == 0);
    assert(next_batch_gpu_capacity(1) == 64);
    assert(next_batch_gpu_capacity(64) == 64);
    assert(next_batch_gpu_capacity(65) == 128);

    const std::array<BatchUploadRange, 4> merge_ranges{{
        {2, 2}, {4, 3}, {10, 2}, {11, 3}
    }};
    auto merged_upload = plan_batch_upload(merge_ranges, 32);
    assert(!merged_upload.full);
    assert(merged_upload.dirty_instances == 9);
    assert(merged_upload.ranges.size() == 2);
    assert(merged_upload.ranges[0].first_instance == 2);
    assert(merged_upload.ranges[0].instance_count == 5);
    assert(merged_upload.ranges[1].first_instance == 10);
    assert(merged_upload.ranges[1].instance_count == 4);

    const std::array<BatchUploadRange, 2> dense_ranges{{{0, 4}, {8, 4}}};
    const auto dense_upload = plan_batch_upload(dense_ranges, 16);
    assert(dense_upload.full);
    assert(dense_upload.dirty_instances == 16);

    std::array<BatchUploadRange, 9> fragmented_ranges{};
    for (std::uint32_t i = 0; i < fragmented_ranges.size(); ++i)
        fragmented_ranges[i] = {i * 3, 1};
    const auto fragmented_upload = plan_batch_upload(fragmented_ranges, 64);
    assert(fragmented_upload.full);
    assert(fragmented_upload.ranges.empty());

    const auto forced_upload = plan_batch_upload(
        std::span<const BatchUploadRange>{}, 17, true);
    assert(forced_upload.full);
    assert(forced_upload.dirty_instances == 17);

    Aoe2DirtyQueue dirty_queue;
    constexpr std::uint32_t dirty_entity_count = 30'000;
    for (std::uint32_t raw = 0; raw < dirty_entity_count; ++raw) {
        const auto entity = static_cast<entt::entity>(raw);
        dirty_queue.enqueue(entity, Aoe2RenderDirty::Frame);
        dirty_queue.enqueue(entity, Aoe2RenderDirty::Transform);
    }
    assert(dirty_queue.pending.size() == dirty_entity_count);
    assert(dirty_queue.enqueued == dirty_entity_count * 2u);
    assert(dirty_queue.deduplicated == dirty_entity_count);
    dirty_queue.begin_processing();
    assert(dirty_queue.pending.empty());
    assert(dirty_queue.processing.size() == dirty_entity_count);
    for (const auto& entry : dirty_queue.processing) {
        const auto mask = static_cast<std::uint8_t>(
            dirty_queue.processing_dirty(entry.entity));
        assert((mask & static_cast<std::uint8_t>(Aoe2RenderDirty::Frame)) != 0u);
        assert((mask & static_cast<std::uint8_t>(Aoe2RenderDirty::Transform)) != 0u);
    }
    dirty_queue.end_processing();
    assert(dirty_queue.processing.empty());

    const auto reused_entity = static_cast<entt::entity>(17u);
    dirty_queue.enqueue(reused_entity, Aoe2RenderDirty::Material);
    assert(dirty_queue.contains(reused_entity));
    dirty_queue.begin_processing();
    assert(dirty_queue.processing_dirty(reused_entity) == Aoe2RenderDirty::Material);
    dirty_queue.end_processing();

    // An entity index may be recycled with a newer version before the queue is
    // drained. Only the new entity may inherit the indexed mask.
    entt::registry recycled_registry;
    const auto old_version = recycled_registry.create();
    recycled_registry.destroy(old_version);
    const auto new_version = recycled_registry.create();
    assert(entt::to_entity(old_version) == entt::to_entity(new_version));
    assert(old_version != new_version);
    dirty_queue.enqueue(old_version, Aoe2RenderDirty::Frame);
    dirty_queue.enqueue(new_version, Aoe2RenderDirty::Transform);
    assert(!dirty_queue.contains(old_version));
    assert(dirty_queue.contains(new_version));
    dirty_queue.begin_processing();
    assert(dirty_queue.processing_dirty(old_version) == Aoe2RenderDirty::None);
    assert(dirty_queue.processing_dirty(new_version) == Aoe2RenderDirty::Transform);
    dirty_queue.end_processing();

    const std::array<BatchUploadRange, 1> sixty_percent{{{0, 6}}};
    const auto aoe_sparse = plan_batch_upload(
        sixty_percent, 10, false, BatchUploadPolicy{8, 75});
    assert(!aoe_sparse.full);
    assert(aoe_sparse.dirty_instances == 6);

    Animation resolved_animation;
    resolved_animation.direction_count = 1;
    resolved_animation.frames_per_direction = 3;
    resolved_animation.main.status = LayerStatus::Partial;
    resolved_animation.shadow.status = LayerStatus::Partial;
    for (int frame = 0; frame < 3; ++frame) {
        Frame value;
        value.direction = 0;
        value.frame = frame;
        value.present = frame != 1;
        value.width = 10 + frame;
        value.height = 20 + frame;
        value.foot = {3.f + frame, 7.f + frame};
        value.uv = {frame * 0.1f, 0.f, 0.1f, 0.2f};
        resolved_animation.main.frames.push_back(value);
        value.width += 5;
        resolved_animation.shadow.frames.push_back(value);
    }
    const auto resolved_table = build_resolved_frame_table(resolved_animation);
    assert(resolved_table.layer_stride == 3);
    assert(resolved_table.records.size() == 6);
    // Equal-distance missing frames resolve to the previous frame, matching
    // the runtime nearest-present rule.
    assert(resolved_table.records[1].size_and_foot.x == 10.f);
    assert(resolved_table.records[4].size_and_foot.x == 15.f);

    EcsWorld transform_world;
    register_transform_lifecycle(transform_world);
    const auto parent = transform_world.spawn();
    transform_world.reg().emplace<Transform>(parent,
        Transform::from_trs({2.f, 0.f, 0.f}));
    const auto child = transform_world.spawn();
    transform_world.reg().emplace<Transform>(child,
        Transform::from_trs({1.f, 0.f, 0.f}));
    assert(set_parent(transform_world, child, parent));
    assert(!set_parent(transform_world, parent, child));
    transform_propagate_system(transform_world);
    assert(transform_world.resource<TransformDiagnostics>().visited == 2);
    assert(std::abs(transform_world.reg().get<GlobalTransform>(child).world[3].x - 3.f) < 0.0001f);
    transform_propagate_system(transform_world);
    assert(transform_world.resource<TransformDiagnostics>().visited == 0);
    assert(patch_transform(transform_world, parent, [](TransformEditor& transform) {
        transform.set_translation({4.f, 0.f, 0.f});
    }));
    transform_propagate_system(transform_world);
    assert(transform_world.resource<TransformDiagnostics>().roots == 1);
    assert(transform_world.resource<TransformDiagnostics>().visited == 2);
    assert(std::abs(transform_world.reg().get<GlobalTransform>(child).world[3].x - 5.f) < 0.0001f);
    transform_world.despawn(parent);
    assert(!transform_world.reg().all_of<Parent>(child));
    transform_propagate_system(transform_world);
    assert(transform_world.resource<TransformDiagnostics>().visited == 1);
    assert(std::abs(transform_world.reg().get<GlobalTransform>(child).world[3].x - 1.f) < 0.0001f);
    disconnect_transform_lifecycle(transform_world);

    auto synchronized_unit_signature = [](std::uint64_t revision) {
        std::uint64_t signature = BatchSignatureSeed;
        for (std::uint32_t entity = 3; entity < 19; ++entity)
            signature = batch_signature_append_source(signature, entity, revision, 1);
        return signature;
    };

    // Regression: summing the old per-source FNV hashes made most adjacent
    // revisions of 16 consecutive, synchronized entities collide. Every
    // logical animation revision must now dirty the batch.
    auto previous = synchronized_unit_signature(1);
    for (std::uint64_t revision = 2; revision <= 500; ++revision) {
        const auto current = synchronized_unit_signature(revision);
        assert(current != previous);
        previous = current;
    }

    std::uint64_t unchanged = BatchSignatureSeed;
    std::uint64_t one_changed = BatchSignatureSeed;
    for (std::uint32_t entity = 3; entity < 19; ++entity) {
        unchanged = batch_signature_append_source(unchanged, entity, 7, 1);
        one_changed = batch_signature_append_source(
            one_changed, entity, entity == 11 ? 8 : 7, 1);
    }
    assert(unchanged != one_changed);

    std::uint64_t forward = BatchSignatureSeed;
    std::uint64_t reverse = BatchSignatureSeed;
    for (std::uint32_t entity = 3; entity < 19; ++entity)
        forward = batch_signature_append_source(forward, entity, 7, 1);
    for (std::uint32_t entity = 19; entity-- > 3;)
        reverse = batch_signature_append_source(reverse, entity, 7, 1);
    assert(forward != reverse);

    static_assert(texture_channel_count(Channels::Gray) == 1);
    static_assert(texture_channel_count(Channels::RG) == 2);
    static_assert(texture_channel_count(Channels::RGBA) == 4);
    static_assert(valid_channel_mapping(Channels::Gray, TextureChannelMapping::Red));
    static_assert(valid_channel_mapping(Channels::RG, TextureChannelMapping::RedAlpha));
    static_assert(!valid_channel_mapping(Channels::RGBA, TextureChannelMapping::Red));

    const TextureDesc gray_default("mask.png", Channels::Gray, false, false, false);
    const TextureDesc source_red("mask.png", Channels::Gray, false, false, false,
                                 TextureChannelMapping::Red);
    assert(gray_default.key() != source_red.key());

    const TextureDesc rg_default("mask.png", Channels::RG, false, false, false);
    const TextureDesc source_red_alpha("mask.png", Channels::RG, false, false, false,
                                       TextureChannelMapping::RedAlpha);
    assert(rg_default.key() != source_red_alpha.key());

    const std::array<unsigned char, 12> rgba = {
        7, 91, 37, 255, // maximum subcolor, covered
        0, 44, 22, 255, // subcolor zero must remain distinguishable from no coverage
        3, 11, 99,   0  // transparent mask pixel
    };
    const auto red = detail::pack_texture_channels(
        rgba, 3, TextureChannelMapping::Red);
    assert((red == std::vector<unsigned char>{7, 0, 3}));
    const auto red_alpha = detail::pack_texture_channels(
        rgba, 3, TextureChannelMapping::RedAlpha);
    assert((red_alpha == std::vector<unsigned char>{7, 255, 0, 255, 3, 0}));
    return 0;
}
