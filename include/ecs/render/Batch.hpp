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
#include <algorithm>
#include <limits>
#include <span>

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

    struct BatchUploadRange {
        std::uint32_t first_instance = 0;
        std::uint32_t instance_count = 0;
    };

    struct BatchUploadPlan {
        bool full = false;
        std::size_t dirty_instances = 0;
        std::vector<BatchUploadRange> ranges;
    };

    inline constexpr std::size_t MaxPartialBatchUploadRanges = 8;

    // Pure upload planner shared by the renderer and runtime tests. Sparse,
    // compact edits remain partial; dense or fragmented edits deliberately
    // collapse to one full upload to avoid excessive driver calls.
    inline BatchUploadPlan plan_batch_upload(std::span<const BatchUploadRange> input,
                                             std::size_t instance_count,
                                             bool force_full = false) {
        BatchUploadPlan plan;
        if (instance_count == 0) return plan;
        if (force_full) {
            plan.full = true;
            plan.dirty_instances = instance_count;
            return plan;
        }

        plan.ranges.reserve(input.size());
        for (const auto& range : input) {
            if (range.instance_count == 0 || range.first_instance >= instance_count) continue;
            const auto available = instance_count - range.first_instance;
            plan.ranges.push_back({
                range.first_instance,
                static_cast<std::uint32_t>(std::min<std::size_t>(range.instance_count, available))
            });
        }
        if (plan.ranges.empty()) return plan;

        std::sort(plan.ranges.begin(), plan.ranges.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first_instance < rhs.first_instance;
        });
        std::size_t output = 0;
        for (const auto& range : plan.ranges) {
            if (output == 0) {
                plan.ranges[output++] = range;
                continue;
            }
            auto& previous = plan.ranges[output - 1];
            const std::uint64_t previous_end =
                static_cast<std::uint64_t>(previous.first_instance) + previous.instance_count;
            const std::uint64_t range_end =
                static_cast<std::uint64_t>(range.first_instance) + range.instance_count;
            if (range.first_instance <= previous_end) {
                previous.instance_count = static_cast<std::uint32_t>(
                    std::max(previous_end, range_end) - previous.first_instance);
            } else {
                plan.ranges[output++] = range;
            }
        }
        plan.ranges.resize(output);
        for (const auto& range : plan.ranges) plan.dirty_instances += range.instance_count;
        if (plan.dirty_instances * 2 >= instance_count ||
            plan.ranges.size() > MaxPartialBatchUploadRanges) {
            plan.full = true;
            plan.dirty_instances = instance_count;
            plan.ranges.clear();
        }
        return plan;
    }

    inline std::size_t next_batch_gpu_capacity(std::size_t required) {
        if (required == 0) return 0;
        std::size_t capacity = 64;
        while (capacity < required && capacity <= std::numeric_limits<std::size_t>::max() / 2)
            capacity *= 2;
        return std::max(capacity, required);
    }

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
    // collector system. Existing collectors can gate full rebuilds with the
    // integer signature; persistent collectors can instead mark sparse ranges.
    // Neither path hashes the output instance bytes.
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
        // Optional sparse updates. Existing collectors leave this empty and
        // retain the original full-upload behaviour through dirty=true.
        std::vector<BatchUploadRange> dirty_ranges;

        unsigned int vao = 0;                 // GL: created lazily at render
        unsigned int instance_vbo = 0;
        std::size_t  gpu_cap = 0;             // instances currently allocated on GPU

        std::uint64_t sig = 0;                // source signature at last rebuild
        std::size_t   count = 0;              // contributing source count at last rebuild
        bool dirty = true;                    // needs GPU re-upload
        bool used = false;                    // touched this frame (GC bookkeeping)
    };

    inline void mark_batch_full_dirty(BatchComponent& batch) {
        batch.dirty = true;
        batch.dirty_ranges.clear();
    }

    inline void mark_batch_instances_dirty(BatchComponent& batch,
                                           std::uint32_t first,
                                           std::uint32_t count) {
        if (batch.dirty || count == 0) return;
        if (!batch.dirty_ranges.empty()) {
            auto& last = batch.dirty_ranges.back();
            const std::uint64_t last_end =
                static_cast<std::uint64_t>(last.first_instance) + last.instance_count;
            const std::uint64_t new_end = static_cast<std::uint64_t>(first) + count;
            // AoE2 visits persistent slots in their insertion order, so the
            // overwhelmingly common path coalesces online to one range/group.
            if (first >= last.first_instance && first <= last_end) {
                last.instance_count = static_cast<std::uint32_t>(
                    std::max(last_end, new_end) - last.first_instance);
                return;
            }
        }
        batch.dirty_ranges.push_back({first, count});
    }

    inline void mark_batch_instance_dirty(BatchComponent& batch, std::uint32_t index) {
        mark_batch_instances_dirty(batch, index, 1);
    }

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
