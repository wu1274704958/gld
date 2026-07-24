// Text batching system: collect batchable glyphs from TextLayout entities into
// BatchComponent entities (grouped by atlas + shader + layer, all as GL ids). A
// batch is only rebuilt/re-uploaded when its integer source signature (folded
// from each contributing entity's TextLayout.src_rev, GlobalTransform.version
// and TextMaterial.rev) changes — no hashing of the output instance bytes.
//
// Shader selection is per-Text: an optional TextMaterial picks a custom fragment
// shader (and per-instance params); without it a Text uses the default text
// shader. Different shaders => different GL program ids => different batches.

#include <vector>
#include <unordered_map>
#include <memory>

#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include <ecs/render/Batch.hpp>
#include <ecs/render/BatchSystem.hpp>
#include <ecs/render/RenderComponents.hpp>   // RenderLayer
#include <ecs/text/TextComponents.hpp>       // TextLayout, TextMaterial
#include <ecs/Components.hpp>                 // GlobalTransform
#include <ecs/EcsWorld.hpp>
#include <ecs/PerformanceMonitoring.hpp>

namespace gld::ecs {

    void text_batch_system(EcsWorld& w) {
        auto& res = w.resource_or_add<BatchResources>();
        Program* aa_shader  = res.aa_shader.get();
        Program* sdf_shader = res.sdf_shader.get();
        if (!aa_shader || !sdf_shader) return;   // text shaders not loaded yet

        auto& reg = w.reg();
        auto& index = w.resource_or_add<TextBatchIndex>();
        auto& diag = w.resource_or_add<RenderDiagnostics>();

        // Reset per-frame liveness.
        for (auto& [k, e] : index.map)
            if (reg.valid(e)) reg.get<BatchComponent>(e).used = false;

        // ---- Pass 1: fold signatures + members per (atlasId, shaderId, layer) ----
        struct Accum {
            std::uint64_t sig = BatchSignatureSeed;
            std::size_t count = 0;
            std::vector<entt::entity> members;
            Program* prog = nullptr;              // binding program (shared by group)
            std::shared_ptr<void> atlas_ref;      // keepalive for the atlas texture
        };
        std::unordered_map<BatchKey, Accum, BatchKeyHash> groups;

        auto view = reg.view<TextLayout, GlobalTransform>();
        for (auto e : view) {
            auto& tl = view.get<TextLayout>(e);
            if (tl.quads.empty()) continue;
            auto& gt = view.get<GlobalTransform>(e);

            std::uint32_t layerMask = 0x1u;
            if (auto* rl = reg.try_get<RenderLayer>(e)) layerMask = rl->mask;

            // Per-entity shader: explicit TextMaterial override wins; else the
            // backend the layout resolved picks AA vs SDF shader.
            Program* prog = (tl.backend == TextRenderMode::SDF) ? sdf_shader : aa_shader;
            std::uint32_t mat_rev = 0;
            if (auto* tm = reg.try_get<TextMaterial>(e)) {
                if (Program* mp = tm->shader.get()) prog = mp;
                mat_rev = tm->rev;
            }
            if (auto* fx = reg.try_get<TextEffects>(e)) mat_rev ^= (fx->rev * 2654435761u);
            const unsigned int shaderId = static_cast<unsigned int>(*prog);

            std::uint64_t m = batch_signature_append_source(
                BatchSignatureSeed, static_cast<std::uint32_t>(entt::to_integral(e)),
                tl.src_rev, gt.version);
            if (mat_rev)
                m = batch_signature_append_source(m, 0xFFFFFFFFu, mat_rev, shaderId);

            // Each distinct atlas this entity touches becomes/updates a group.
            std::vector<unsigned int> seen;
            for (const auto& q : tl.quads) {
                const unsigned int atlasId = q.atlas ? q.atlas->get_id() : 0u;
                bool have = false;
                for (auto id : seen) if (id == atlasId) { have = true; break; }
                if (have) continue;
                seen.push_back(atlasId);

                BatchKey key;
                key.textures[0] = atlasId;
                key.texture_count = 1;
                key.shader = shaderId;
                key.layers = layerMask;
                Accum& acc = groups[key];
                acc.sig = batch_signature_append(acc.sig, m);
                acc.count += 1;
                acc.members.push_back(e);
                acc.prog = prog;
                if (!acc.atlas_ref) acc.atlas_ref = q.atlas;   // shared_ptr<Texture> -> void
            }
        }

        // ---- Pass 2: create/update batch entities; rebuild only changed groups ----
        for (auto& [key, acc] : groups) {
            entt::entity be;
            auto it = index.map.find(key);
            if (it == index.map.end() || !reg.valid(it->second)) {
                be = reg.create();
                reg.emplace<BatchComponent>(be);
                index.map[key] = be;
            } else {
                be = it->second;
            }

            auto& bc = reg.get<BatchComponent>(be);
            bc.key = key;
            bc.layers = key.layers;
            bc.prog = acc.prog;
            clear_batch_textures(bc);
            int diffuse_loc = acc.prog->uniform_id("diffuseTex");
            if (diffuse_loc < 0) {
                acc.prog->locat_uniforms("diffuseTex");
                diffuse_loc = acc.prog->uniform_id("diffuseTex");
            }
            set_batch_texture(bc, 0, key.textures[0], acc.atlas_ref, diffuse_loc);
            bc.used = true;

            if (bc.sig != acc.sig || bc.count != acc.count) {
                bc.instances.clear();
                for (auto me : acc.members) {
                    auto& tl = reg.get<TextLayout>(me);
                    auto& gt = reg.get<GlobalTransform>(me);

                    // Pack effect params (per entity) into the instance params.
                    glm::vec4 mp0(0.f), mp1(0.f), mp2(0.f), mp3(0.f);
                    if (auto* fx = reg.try_get<TextEffects>(me)) {
                        int flags = 0;
                        if (fx->outline) flags |= 1;
                        if (fx->shadow)  flags |= 2;
                        mp0 = fx->outline_color;
                        mp1 = glm::vec4(fx->outline_width, fx->shadow_softness,
                                        static_cast<float>(flags), 0.f);
                        mp2 = fx->shadow_color;
                        mp3 = glm::vec4(fx->shadow_offset.x, fx->shadow_offset.y, 0.f, 0.f);
                    } else if (auto* tm = reg.try_get<TextMaterial>(me)) {
                        mp0 = tm->mparam0; mp1 = tm->mparam1;  // custom-material params
                    }

                    for (const auto& q : tl.quads) {
                        const unsigned int atlasId = q.atlas ? q.atlas->get_id() : 0u;
                        if (atlasId != key.textures[0]) continue;
                        InstanceData d;
                        d.rect = q.rect;
                        d.pad = q.pad;
                        d.color = q.color;
                        d.transform = gt.world * q.local;
                        d.mparam0 = mp0;
                        d.mparam1 = mp1;
                        d.mparam2 = mp2;
                        d.mparam3 = mp3;
                        bc.instances.push_back(d);
                    }
                }
                bc.sig = acc.sig;
                bc.count = acc.count;
                bc.dirty = true;
            }
        }

        // ---- Pass 3: GC batches whose group vanished this frame ----
        std::vector<BatchKey> dead;
        for (auto& [k, e] : index.map) {
            if (!reg.valid(e)) {
                dead.push_back(k);
                continue;
            }
            auto& bc = reg.get<BatchComponent>(e);
            if (!bc.used) {
                destroy_batch_gpu(bc);
                reg.destroy(e);
                dead.push_back(k);
                GLD_PERF_MONITOR(++diag.batch_groups_destroyed);
            }
        }
        for (const auto& k : dead) index.map.erase(k);
    }
}
