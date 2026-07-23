#pragma once

// Instanced-quad batching – data layer.
//
// A batch groups textured unit-quads that share the SAME (texture set, shader,
// layers) into one instanced draw. Up to four textures are bound per batch;
// the original one-texture path remains allocation-free and has a draw fast path.
// Batches are ECS COMPONENTS (BatchComponent),
// produced by per-type collector systems (text_batch_system today; a future
// sprite_batch_system plugs in the same way) and drawn generically by the
// renderer via view<BatchComponent>.
//
// InstanceData mirrors the attribute layout of res/ecs/text_vs.glsl
// (loc1=uv, loc2=uv2, loc3=color, loc4..7=model, loc8..11=local), so one
// instanced shader draws every batch.

#include <cstdint>
#include <array>
#include <vector>
#include <memory>
#include <utility>
#include <cassert>

#include <glm/glm.hpp>

namespace gld { class Program; }   // fwd: BatchComponent keeps a Program* for binding

namespace gld::ecs {

    inline constexpr std::size_t MaxBatchTextures = 4;

    enum class BatchStateOverride : std::uint8_t {
        Inherit,
        Enabled,
        Disabled
    };

    inline bool resolve_batch_state(BatchStateOverride value, bool inherited) {
        switch (value) {
        case BatchStateOverride::Enabled: return true;
        case BatchStateOverride::Disabled: return false;
        case BatchStateOverride::Inherit:
        default: return inherited;
        }
    }

    struct InstanceData {
        glm::vec4 rect{ 0.f };  // glyph atlas uv rect (x,y,w,h) — fg sampling clamp
        glm::vec4 pad{ 0.f };   // shadow-expanded uv rect (x,y,w,h) — quad corner uv
        glm::vec4 color{ 1.f };
        glm::mat4 transform{ 1.f };  // world * local (merged; entity world + glyph quad)
        glm::vec4 mparam0{ 0.f };    // material param 0 (e.g. outline colour)
        glm::vec4 mparam1{ 0.f };    // (outline_width_px, shadow_softness, flags, 0)
        glm::vec4 mparam2{ 0.f };    // shadow colour rgba
        glm::vec4 mparam3{ 0.f };    // (shadow_off_x_px, shadow_off_y_px, 0, 0)
    };

    // Grouping key: same texture slots + same shader + same layer mask => one batch.
    // Texture / shader fields are GL object ids (not pointers): a GL id is the
    // resource's true identity, immune to the pointer-reuse aliasing a
    // freed+realloced object could cause.
    struct BatchKey {
        std::array<unsigned int, MaxBatchTextures> textures{}; // GL ids, slots 0..texture_count-1
        std::uint8_t texture_count = 0;
        unsigned int shader = 0;            // program GL id
        std::uint32_t layers = 0xFFFFFFFFu; // camera culling mask this batch belongs to
        BatchStateOverride depth_test = BatchStateOverride::Inherit;
        BatchStateOverride depth_write = BatchStateOverride::Inherit;
        bool operator==(const BatchKey& o) const {
            if (texture_count != o.texture_count || shader != o.shader || layers != o.layers ||
                depth_test != o.depth_test || depth_write != o.depth_write)
                return false;
            for (std::size_t i = 0; i < texture_count; ++i)
                if (textures[i] != o.textures[i]) return false;
            return true;
        }
    };
    struct BatchKeyHash {
        std::size_t operator()(const BatchKey& k) const {
            std::size_t h = std::hash<std::uint8_t>{}(k.texture_count);
            auto mix = [&h](std::size_t v) {
                h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            };
            for (std::size_t i = 0; i < k.texture_count; ++i)
                mix(std::hash<unsigned int>{}(k.textures[i]));
            mix(std::hash<unsigned int>{}(k.shader));
            mix(std::hash<std::uint32_t>{}(k.layers));
            mix(std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(k.depth_test)));
            mix(std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(k.depth_write)));
            return h;
        }
    };

    // One batch = one instanced draw. Lives on a batch entity, maintained by a
    // collector system. Dirty/upload is decided by a cheap integer signature
    // (sig,count) over the contributing sources — never by hashing the output
    // instance bytes.
    struct BatchComponent {
        BatchComponent() = default;
        ~BatchComponent();
        BatchComponent(const BatchComponent&) = delete;
        BatchComponent& operator=(const BatchComponent&) = delete;
        BatchComponent(BatchComponent&& other) noexcept;
        BatchComponent& operator=(BatchComponent&& other) noexcept;

        BatchKey key;
        std::uint32_t layers = 0xFFFFFFFFu;   // == key.layers (kept for fast filtering)

        // Bind handles (key only holds GL ids). Texture slots are fixed-size so
        // the original one-texture path never allocates and InstanceData stays unchanged.
        gld::Program* prog = nullptr;
        std::array<std::shared_ptr<void>, MaxBatchTextures> texture_refs{};
        std::array<int, MaxBatchTextures> sampler_locations{ -1, -1, -1, -1 };
        int order = 0;                       // lower values draw first within the batch pass

        std::vector<InstanceData> instances;  // CPU side

        unsigned int vao = 0;                 // GL: created lazily at render
        unsigned int instance_vbo = 0;
        std::size_t  gpu_cap = 0;             // instances currently allocated on GPU

        std::uint64_t sig = 0;                // source signature at last rebuild
        std::size_t   count = 0;              // contributing source count at last rebuild
        bool dirty = true;                    // needs GPU re-upload
        bool used = false;                    // touched this frame (GC bookkeeping)
    };

    inline void clear_batch_textures(BatchComponent& batch) {
        batch.key.texture_count = 0;
        batch.key.textures.fill(0);
        batch.texture_refs.fill(nullptr);
        batch.sampler_locations.fill(-1);
    }

    inline void set_batch_texture(BatchComponent& batch, std::size_t slot,
                                  unsigned int texture_id, std::shared_ptr<void> keepalive,
                                  int sampler_location) {
        assert(slot < MaxBatchTextures && "BatchComponent supports at most four texture slots");
        if (slot >= MaxBatchTextures) return;
        batch.key.textures[slot] = texture_id;
        batch.texture_refs[slot] = std::move(keepalive);
        batch.sampler_locations[slot] = sampler_location;
        if (batch.key.texture_count < slot + 1)
            batch.key.texture_count = static_cast<std::uint8_t>(slot + 1);
    }

    inline constexpr std::uint64_t BatchSignatureSeed = 1469598103934665603ull;
    inline constexpr std::uint64_t BatchSignaturePrime = 1099511628211ull;

    // Append values in the same order in which instances will be emitted.
    // A commutative sum of independently mixed sources is unsafe here: when a
    // run of entities advances the same revision in lockstep, the individual
    // hashes can cancel and leave the aggregate unchanged. A source-order
    // change also changes the instance stream, so an order-sensitive signature
    // is the correct dirty-gating semantic.
    inline std::uint64_t batch_signature_append(std::uint64_t signature,
                                                std::uint64_t value) {
        return (signature ^ value) * BatchSignaturePrime;
    }

    inline std::uint64_t batch_signature_append_source(std::uint64_t signature,
                                                       std::uint32_t entity_raw,
                                                       std::uint64_t rev,
                                                       std::uint32_t ver) {
        signature = batch_signature_append(signature, entity_raw);
        signature = batch_signature_append(signature, rev);
        return batch_signature_append(signature, ver);
    }

    // One-source helper retained for callers that need a standalone value.
    inline std::uint64_t batch_mix(std::uint32_t entity_raw, std::uint64_t rev, std::uint32_t ver) {
        return batch_signature_append_source(BatchSignatureSeed, entity_raw, rev, ver);
    }
}
