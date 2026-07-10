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

namespace gld::ecs {

    void text_batch_system(EcsWorld& w) {
        auto& res = w.resource_or_add<BatchResources>();
        Program* def_shader = res.text_shader.get();
        if (!def_shader) return;             // default text shader not loaded yet

        auto& reg = w.reg();
        auto& index = w.resource_or_add<TextBatchIndex>();

        // Reset per-frame liveness.
        for (auto& [k, e] : index.map)
            if (reg.valid(e)) reg.get<BatchComponent>(e).used = false;

        // ---- Pass 1: fold signatures + members per (atlasId, shaderId, layer) ----
        struct Accum {
            std::uint64_t sig = 0;
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

            // Per-entity shader: TextMaterial's (if loaded) else the default.
            Program* prog = def_shader;
            std::uint32_t mat_rev = 0;
            if (auto* tm = reg.try_get<TextMaterial>(e)) {
                if (Program* mp = tm->shader.get()) prog = mp;
                mat_rev = tm->rev;
            }
            const unsigned int shaderId = static_cast<unsigned int>(*prog);

            std::uint64_t m = batch_mix(
                static_cast<std::uint32_t>(entt::to_integral(e)), tl.src_rev, gt.version);
            if (mat_rev) m ^= batch_mix(0xFFFFFFFFu, mat_rev, shaderId);

            // Each distinct atlas this entity touches becomes/updates a group.
            std::vector<unsigned int> seen;
            for (const auto& q : tl.quads) {
                const unsigned int atlasId = q.atlas ? q.atlas->get_id() : 0u;
                bool have = false;
                for (auto id : seen) if (id == atlasId) { have = true; break; }
                if (have) continue;
                seen.push_back(atlasId);

                BatchKey key{ atlasId, shaderId, layerMask };
                Accum& acc = groups[key];
                acc.sig += m;
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
            bc.atlas_ref = acc.atlas_ref;
            bc.used = true;

            if (bc.sig != acc.sig || bc.count != acc.count) {
                bc.instances.clear();
                for (auto me : acc.members) {
                    auto& tl = reg.get<TextLayout>(me);
                    auto& gt = reg.get<GlobalTransform>(me);

                    glm::vec4 mp0(0.f), mp1(0.f);
                    if (auto* tm = reg.try_get<TextMaterial>(me)) { mp0 = tm->mparam0; mp1 = tm->mparam1; }

                    for (const auto& q : tl.quads) {
                        const unsigned int atlasId = q.atlas ? q.atlas->get_id() : 0u;
                        if (atlasId != key.atlas) continue;
                        InstanceData d;
                        const float rx = q.uv.x, ry = q.uv.y, rz = q.uv.z, rw = q.uv.w;
                        d.uv  = glm::vec4(rx + rz, ry,      rx,      ry);
                        d.uv2 = glm::vec4(rx,      ry + rw, rx + rz, ry + rw);
                        d.color = q.color;
                        d.model = gt.world;
                        d.local = q.local;
                        d.mparam0 = mp0;
                        d.mparam1 = mp1;
                        bc.instances.push_back(d);
                    }
                }
                bc.sig = acc.sig;
                bc.count = acc.count;
                bc.dirty = true;
            }
        }

        // ---- Pass 3: GC batches whose group vanished this frame ----
        for (auto& [k, e] : index.map) {
            if (!reg.valid(e)) continue;
            auto& bc = reg.get<BatchComponent>(e);
            if (!bc.used && !bc.instances.empty()) {
                bc.instances.clear();
                bc.sig = 0;
                bc.count = 0;
                bc.dirty = true;
            }
        }
    }
}
