#include <aoe2/Aoe2Systems.hpp>

#include <algorithm>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <ecs/Components.hpp>
#include <ecs/render/BatchSystem.hpp>
#include <ecs/render/RenderComponents.hpp>

namespace gld::ecs::aoe2 {
namespace {
struct Source { entt::entity entity; bool shadow; };
struct Accum {
    std::uint64_t signature = BatchSignatureSeed;
    std::vector<Source> sources;
    Program* program = nullptr;
    std::array<std::shared_ptr<void>, MaxBatchTextures> refs{};
    std::array<int, MaxBatchTextures> sampler_locations{-1, -1, -1, -1};
    int order = 0;
};

int sampler(Program& program, const char* name) {
    int location = program.uniform_id(name);
    if (location < 0) {
        program.locat_uniforms(name);
        location = program.uniform_id(name);
    }
    return location;
}

glm::mat4 frame_transform(const glm::mat4& world, const Frame& frame) {
    const glm::vec3 offset(frame.width * .5f - frame.foot.x,
                           frame.foot.y - frame.height * .5f, 0.f);
    return world * glm::translate(glm::mat4(1.f), offset)
        * glm::scale(glm::mat4(1.f), glm::vec3(frame.width, frame.height, 1.f));
}
} // namespace

void aoe2_batch_system(EcsWorld& world) {
    auto& resources = world.resource_or_add<Aoe2RenderResources>();
    Program* sprite_program = resources.sprite_shader.get();
    Program* player_program = resources.player_color_shader.get();
    Program* shadow_program = resources.shadow_shader.get();
    if (!sprite_program || !player_program || !shadow_program || !resources.palette_texture) return;

    auto& reg = world.reg();
    auto& index = world.resource_or_add<Aoe2BatchIndex>();
    auto& diagnostics = world.resource_or_add<RenderDiagnostics>();
    for (auto& [_, entity] : index.map)
        if (reg.valid(entity)) reg.get<BatchComponent>(entity).used = false;

    std::unordered_map<BatchKey, Accum, BatchKeyHash> groups;
    for (auto entity : reg.view<Aoe2UnitRender, GlobalTransform>()) {
        const auto& render = reg.get<Aoe2UnitRender>(entity);
        const auto& global = reg.get<GlobalTransform>(entity);
        if (!render.visible || !render.has_main) continue;
        auto* appearance = render.appearance.get();
        if (!appearance) continue;
        const auto* animation = appearance->find_animation(render.animation);
        if (!animation) continue;
        const std::uint32_t layers = reg.try_get<RenderLayer>(entity)
            ? reg.get<RenderLayer>(entity).mask : 0x1u;
        const auto entity_raw = static_cast<std::uint32_t>(entt::to_integral(entity));

        if (render.has_shadow) {
            auto* texture = animation->shadow.texture.get();
            if (texture) {
                BatchKey key;
                key.texture_count = 1;
                key.textures[0] = texture->get_id();
                key.shader = static_cast<unsigned int>(*shadow_program);
                key.layers = layers;
                auto& group = groups[key];
                group.signature = batch_signature_append_source(
                    group.signature, entity_raw, render.revision, global.version);
                group.sources.push_back({entity, true});
                group.program = shadow_program;
                group.refs[0] = animation->shadow.texture.shared();
                group.sampler_locations[0] = sampler(*shadow_program, "diffuseTex");
                group.order = 0;
            }
        }

        auto* main_texture = animation->main.texture.get();
        if (!main_texture) continue;
        const bool use_player = animation->player_color.usable() &&
            appearance->player_color_format == "r8_subcolor_alpha_binary";
        // Do not flash the un-recoloured main atlas while its declared mask is
        // still loading. A genuinely missing mask uses the one-texture shader.
        if (use_player && !animation->player_color.texture.get()) continue;
        Program* program = use_player ? player_program : sprite_program;
        BatchKey key;
        key.texture_count = use_player ? 3 : 1;
        key.textures[0] = main_texture->get_id();
        if (use_player) {
            key.textures[1] = animation->player_color.texture.get()->get_id();
            key.textures[2] = resources.palette_texture->get_id();
        }
        key.shader = static_cast<unsigned int>(*program);
        key.layers = layers;
        auto& group = groups[key];
        if (group.sources.empty() && use_player)
            group.signature = batch_signature_append(
                group.signature, resources.palette_revision);
        group.signature = batch_signature_append_source(
            group.signature, entity_raw, render.revision, global.version);
        group.sources.push_back({entity, false});
        group.program = program;
        group.refs[0] = animation->main.texture.shared();
        group.sampler_locations[0] = sampler(*program, "diffuseTex");
        if (use_player) {
            group.refs[1] = animation->player_color.texture.shared();
            group.refs[2] = resources.palette_texture;
            group.sampler_locations[1] = sampler(*program, "playerColorMask");
            group.sampler_locations[2] = sampler(*program, "playerPalette");
        }
        group.order = 1;
    }

    for (auto& [key, group] : groups) {
        entt::entity batch_entity;
        const auto found = index.map.find(key);
        if (found == index.map.end() || !reg.valid(found->second)) {
            batch_entity = reg.create();
            reg.emplace<BatchComponent>(batch_entity);
            index.map[key] = batch_entity;
        } else batch_entity = found->second;

        auto& batch = reg.get<BatchComponent>(batch_entity);
        batch.key = key;
        batch.layers = key.layers;
        batch.prog = group.program;
        batch.order = group.order;
        batch.used = true;
        batch.texture_refs = group.refs;
        batch.sampler_locations = group.sampler_locations;
        if (batch.sig == group.signature && batch.count == group.sources.size()) continue;

        batch.instances.clear();
        for (const auto& source : group.sources) {
            const auto& render = reg.get<Aoe2UnitRender>(source.entity);
            const auto& global = reg.get<GlobalTransform>(source.entity);
            InstanceData instance;
            const Frame& frame = source.shadow ? render.shadow_frame : render.main_frame;
            instance.rect = frame.uv;
            instance.pad = frame.uv;
            instance.color = source.shadow ? glm::vec4(1.f, 1.f, 1.f, render.tint.a) : render.tint;
            instance.transform = frame_transform(global.world, frame);
            instance.mparam0.x = static_cast<float>(render.player_color);
            instance.mparam0.y = static_cast<float>(render.player_color_debug);
            batch.instances.push_back(instance);
        }
        batch.sig = group.signature;
        batch.count = group.sources.size();
        batch.dirty = true;
    }

    std::vector<BatchKey> dead;
    for (auto& [key, entity] : index.map) {
        if (!reg.valid(entity)) { dead.push_back(key); continue; }
        auto& batch = reg.get<BatchComponent>(entity);
        if (!batch.used) {
            destroy_batch_gpu(batch);
            reg.destroy(entity);
            dead.push_back(key);
            ++diagnostics.batch_groups_destroyed;
        }
    }
    for (const auto& key : dead) index.map.erase(key);
}

void destroy_aoe2_batches(EcsWorld& world) {
    auto* index = world.try_resource<Aoe2BatchIndex>();
    if (!index) return;
    auto& reg = world.reg();
    for (const auto& [_, entity] : index->map) {
        if (!reg.valid(entity)) continue;
        destroy_batch_gpu(reg.get<BatchComponent>(entity));
        reg.destroy(entity);
    }
    index->map.clear();
}

} // namespace gld::ecs::aoe2
