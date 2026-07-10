// Text batching system: collect batchable glyphs from TextLayout entities into
// BatchComponent entities (grouped by atlas + shader + layer). A batch is only
// rebuilt/re-uploaded when its integer source signature (folded from each
// contributing entity's TextLayout.src_rev + GlobalTransform.version) changes —
// no hashing of the output instance bytes.

#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include <ecs/render/Batch.hpp>
#include <ecs/render/BatchSystem.hpp>
#include <ecs/render/RenderComponents.hpp>   // RenderLayer
#include <ecs/text/TextComponents.hpp>       // TextLayout
#include <ecs/Components.hpp>                 // GlobalTransform
#include <ecs/EcsWorld.hpp>

namespace gld::ecs {

    void text_batch_system(EcsWorld& w) {
        auto& res = w.resource_or_add<BatchResources>();
        Program* shader = res.text_shader.get();
        if (!shader) return;                 // text shader not loaded yet

        auto& reg = w.reg();
        auto& index = w.resource_or_add<TextBatchIndex>();

        // Reset per-frame liveness.
        for (auto& [k, e] : index.map)
            if (reg.valid(e)) reg.get<BatchComponent>(e).used = false;

        // ---- Pass 1: fold source signatures + members per (atlas,shader,layer) ----
        struct Accum { std::uint64_t sig = 0; std::size_t count = 0; std::vector<entt::entity> members; };
        std::unordered_map<BatchKey, Accum, BatchKeyHash> groups;

        auto view = reg.view<TextLayout, GlobalTransform>();
        for (auto e : view) {
            auto& tl = view.get<TextLayout>(e);
            if (tl.quads.empty()) continue;
            auto& gt = view.get<GlobalTransform>(e);

            std::uint32_t layerMask = 0x1u;
            if (auto* rl = reg.try_get<RenderLayer>(e)) layerMask = rl->mask;

            std::uint64_t m = batch_mix(
                static_cast<std::uint32_t>(entt::to_integral(e)), tl.src_rev, gt.version);

            // Each distinct atlas this entity touches becomes/updates a group.
            std::vector<const void*> seen;
            for (const auto& q : tl.quads) {
                const void* atlas = q.atlas.get();
                bool have = false;
                for (auto p : seen) if (p == atlas) { have = true; break; }
                if (have) continue;
                seen.push_back(atlas);

                BatchKey key{ atlas, shader, layerMask };
                Accum& acc = groups[key];
                acc.sig += m;
                acc.count += 1;
                acc.members.push_back(e);
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
            bc.used = true;

            if (bc.sig != acc.sig || bc.count != acc.count) {
                bc.instances.clear();
                for (auto me : acc.members) {
                    auto& tl = reg.get<TextLayout>(me);
                    auto& gt = reg.get<GlobalTransform>(me);
                    for (const auto& q : tl.quads) {
                        if (q.atlas.get() != key.atlas) continue;
                        InstanceData d;
                        const float rx = q.uv.x, ry = q.uv.y, rz = q.uv.z, rw = q.uv.w;
                        d.uv  = glm::vec4(rx + rz, ry,      rx,      ry);
                        d.uv2 = glm::vec4(rx,      ry + rw, rx + rz, ry + rw);
                        d.color = q.color;
                        d.model = gt.world;
                        d.local = q.local;
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
