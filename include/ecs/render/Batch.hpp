#pragma once

// Instanced-quad batching – data layer.
//
// A batch groups textured unit-quads that share the SAME (atlas texture, shader,
// layers) into one instanced draw. Batches are ECS COMPONENTS (BatchComponent),
// produced by per-type collector systems (text_batch_system today; a future
// sprite_batch_system plugs in the same way) and drawn generically by the
// renderer via view<BatchComponent>.
//
// InstanceData mirrors the attribute layout of res/ecs/text_vs.glsl
// (loc1=uv, loc2=uv2, loc3=color, loc4..7=model, loc8..11=local), so one
// instanced shader draws every batch.

#include <cstdint>
#include <vector>
#include <memory>

#include <glm/glm.hpp>

namespace gld { class Program; }   // fwd: BatchComponent keeps a Program* for binding

namespace gld::ecs {

    struct InstanceData {
        glm::vec4 uv{ 0.f };    // packed atlas corners (see text_vs.glsl)
        glm::vec4 uv2{ 0.f };
        glm::vec4 color{ 1.f };
        glm::mat4 model{ 1.f }; // entity world transform (per-instance: a batch mixes parents)
        glm::mat4 local{ 1.f }; // per-glyph/sprite local matrix
        glm::vec4 mparam0{ 0.f }; // material param 0 (forwarded to fg; e.g. outline colour)
        glm::vec4 mparam1{ 0.f }; // material param 1 (e.g. outline width/softness)
    };

    // Grouping key: same texture + same shader + same layer mask => one batch.
    // atlas / shader are GL object ids (not pointers): a GL id is the resource's
    // true identity, immune to the pointer-reuse aliasing a freed+realloced
    // object could cause.
    struct BatchKey {
        unsigned int atlas = 0;             // texture GL id
        unsigned int shader = 0;            // program GL id
        std::uint32_t layers = 0xFFFFFFFFu; // camera culling mask this batch belongs to
        bool operator==(const BatchKey& o) const {
            return atlas == o.atlas && shader == o.shader && layers == o.layers;
        }
    };
    struct BatchKeyHash {
        std::size_t operator()(const BatchKey& k) const {
            std::size_t a = std::hash<unsigned int>{}(k.atlas);
            std::size_t b = std::hash<unsigned int>{}(k.shader);
            std::size_t c = std::hash<std::uint32_t>{}(k.layers);
            std::size_t h = a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2));
            return h ^ (c + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2));
        }
    };

    // One batch = one instanced draw. Lives on a batch entity, maintained by a
    // collector system. Dirty/upload is decided by a cheap integer signature
    // (sig,count) over the contributing sources — never by hashing the output
    // instance bytes.
    struct BatchComponent {
        BatchKey key;
        std::uint32_t layers = 0xFFFFFFFFu;   // == key.layers (kept for fast filtering)

        // Bind handles (key only holds GL ids). prog is used for use()+uniforms;
        // atlas_ref keeps the atlas texture alive (its GL id is key.atlas).
        gld::Program* prog = nullptr;
        std::shared_ptr<void> atlas_ref;

        std::vector<InstanceData> instances;  // CPU side

        unsigned int vao = 0;                 // GL: created lazily at render
        unsigned int instance_vbo = 0;
        std::size_t  gpu_cap = 0;             // instances currently allocated on GPU

        std::uint64_t sig = 0;                // source signature at last rebuild
        std::size_t   count = 0;              // contributing source count at last rebuild
        bool dirty = true;                    // needs GPU re-upload
        bool used = false;                    // touched this frame (GC bookkeeping)
    };

    // Fold one source into a batch signature. Order-independent (summed by the
    // caller), integer-only — no output bytes touched.
    inline std::uint64_t batch_mix(std::uint32_t entity_raw, std::uint64_t rev, std::uint32_t ver) {
        std::uint64_t h = 1469598103934665603ull;
        h = (h ^ entity_raw) * 1099511628211ull;
        h = (h ^ rev)        * 1099511628211ull;
        h = (h ^ ver)        * 1099511628211ull;
        return h;
    }
}
