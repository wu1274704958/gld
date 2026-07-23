#pragma once

// Instanced-quad batching – system layer.
//
//   text_batch_system  (PostUpdate) – collect batchable glyphs from
//                                      TextLayout+GlobalTransform into
//                                      BatchComponent entities. Rebuilds a batch
//                                      only when its integer source signature
//                                      changes (no output byte hashing).
//   draw_batches       (Render)     – draw all BatchComponents a camera's layer
//                                      mask selects; upload GPU only when dirty.
//   TextBatchPlugin                 – shared quad + text shader + text_batch_system.
//
// The renderer is generic (2D textured quads); a future sprite_batch_system
// produces the same BatchComponent and is drawn by the same draw_batches.

#include <unordered_map>

#include <program.hpp>
#include <texture.hpp>

#include "Batch.hpp"
#include "RenderComponents.hpp"
#include "../App.hpp"
#include "../EcsWorld.hpp"
#include "../assets/Handle.hpp"
#include "../assets/AssetServer.hpp"

namespace gld::ecs {

    struct RenderStateCache;

    struct BatchResources {
        BatchResources() = default;
        ~BatchResources();
        BatchResources(const BatchResources&) = delete;
        BatchResources& operator=(const BatchResources&) = delete;

        Handle<Program> aa_shader;   // ecs/text_fg  (AA coverage fill + shadow)
        Handle<Program> sdf_shader;  // ecs/text_sdf_fg (SDF fill + outline + shadow)
        unsigned int quad_vbo = 0;   // shared unit quad (pos only)
        unsigned int quad_ebo = 0;
        int index_count = 6;
        bool quad_ready = false;
    };

    // BatchKey -> batch entity, owned by text_batch_system.
    struct TextBatchIndex {
        std::unordered_map<BatchKey, entt::entity, BatchKeyHash> map;
    };

    // ---- systems ----
    void text_batch_system(EcsWorld& w);                 // src/EcsTextBatch.cpp
    void draw_batches(EcsWorld& w, const Camera& cam, RenderStateCache& state_cache,
                      const ResolvedRenderPassState& pass_state); // src/EcsBatch.cpp
    void destroy_batch_gpu(BatchComponent& b);
    void destroy_batch_resources_gpu(BatchResources& res);

    // ---- plugin ----
    inline void TextBatchPlugin(App& app) {
        auto& res = app.world.resource_or_add<BatchResources>();
        auto& srv = app.world.resource<AssetServer>();
        res.aa_shader  = srv.load_program("ecs/text_vs.glsl", "ecs/text_fg.glsl");
        res.sdf_shader = srv.load_program("ecs/text_vs.glsl", "ecs/text_sdf_fg.glsl");

        app.world.resource_or_add<TextBatchIndex>();

        // PostUpdate: must run after transform_propagate_system (add TransformPlugin first).
        app.add_system(Stage::PostUpdate, text_batch_system);
    }
}
