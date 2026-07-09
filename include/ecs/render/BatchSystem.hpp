#pragma once

// Generic instanced-quad batching – system layer.
//
//   batch_collect<CTS...>  – PostUpdate/Render: rebuild BatchStore each frame by
//                            asking BatchSource<C> for every enabled type C.
//   batch_render_system    – Render: draw every batch (lazy GL quad + per-batch
//                            instance VBO, uploaded only when the content hash
//                            changed) as one glDrawElementsInstanced call.
//   BatchPlugin<CTS...>    – wires the shared quad, the text shader, the view
//                            matrix updater and the collect/render systems.
//
// The renderer is generic (2D textured quads); TextLayout is just the first
// source. To batch Sprites later, add a BatchSource<Sprite> and list it in CTS.

#include <memory>

#include <glm/glm.hpp>
#include <program.hpp>
#include <texture.hpp>

#include "Batch.hpp"
#include "RenderComponents.hpp"
#include "../App.hpp"
#include "../EcsWorld.hpp"
#include "../Components.hpp"          // GlobalTransform
#include "../assets/Handle.hpp"
#include "../assets/AssetServer.hpp"
#include "../text/TextComponents.hpp"

namespace gld::ecs {

    struct BatchResources {
        Handle<Program> text_shader;
        unsigned int quad_vbo = 0;   // shared unit quad (pos only)
        unsigned int quad_ebo = 0;
        int index_count = 6;
        bool quad_ready = false;
    };

    // ---- text source ----
    template<> struct BatchSource<TextLayout> {
        static Program* shader(EcsWorld& w) {
            auto& res = w.resource_or_add<BatchResources>();
            return res.text_shader.get();
        }
        static void collect(entt::entity, const TextLayout& tl, const glm::mat4& world,
                            BatchStore& store, Program* shader) {
            for (const auto& q : tl.quads) {
                InstanceData d;
                const float rx = q.uv.x, ry = q.uv.y, rz = q.uv.z, rw = q.uv.w;
                // pack corners to match text_vs.glsl (v0=TR, v1=TL, v2=BL, v3=BR)
                d.uv  = glm::vec4(rx + rz, ry,      rx,      ry);
                d.uv2 = glm::vec4(rx,      ry + rw, rx + rz, ry + rw);
                d.color = q.color;
                d.model = world;
                d.local = q.local;
                store.append(BatchKey{ q.atlas.get(), shader }, d);
            }
        }
    };

    // ---- collect ----
    template<class C>
    inline void batch_collect_one(EcsWorld& w, BatchStore& store) {
        Program* sh = BatchSource<C>::shader(w);
        if (!sh) return;                        // shader not loaded yet
        auto view = w.reg().template view<C, GlobalTransform>();
        for (auto e : view) {
            auto& c = view.template get<C>(e);
            auto& g = view.template get<GlobalTransform>(e);
            BatchSource<C>::collect(e, c, g.world, store, sh);
        }
    }

    template<class... CTS>
    inline void batch_collect(EcsWorld& w) {
        auto& store = w.resource_or_add<BatchStore>();
        store.begin_frame();
        (batch_collect_one<CTS>(w, store), ...);
        for (auto& [k, b] : store.batches) {
            std::uint64_t h = hash_instances(b.instances);
            if (h != b.content_hash) { b.content_hash = h; b.dirty = true; }
        }
    }

    // ---- GL systems (defined in src/EcsBatch.cpp) ----
    // Draw all collected batches for one 2D (Ortho) camera pass. Render state
    // (blend/depth/target/clear) is owned by render_system; this only issues the
    // instanced draws using cam.projection as uViewProj.
    void draw_batches(EcsWorld& w, const Camera& cam);

    // ---- plugin ----
    // Loads the text shader and registers batch_collect (PostUpdate, after
    // transform propagation). Actual drawing is driven by render_system's Ortho
    // camera passes via draw_batches.
    template<class... CTS>
    inline void BatchPlugin(App& app) {
        auto& res = app.world.resource_or_add<BatchResources>();
        auto& srv = app.world.resource<AssetServer>();
        res.text_shader = srv.load_program("ecs/text_vs.glsl", "ecs/text_fg.glsl");

        app.world.resource_or_add<BatchStore>();

        app.add_system(Stage::PostUpdate, [](EcsWorld& w) { batch_collect<CTS...>(w); });
    }
}
