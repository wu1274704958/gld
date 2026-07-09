#pragma once

// Generic instanced-quad batching – data layer.
//
// A batch groups textured unit-quads that share the SAME (atlas texture, shader)
// into one instanced draw. It is deliberately generic (knows nothing about
// "text"): any component type can feed it by specialising BatchSource<C>. Today
// the only source is TextLayout, but Sprite etc. plug in the same way.
//
// InstanceData mirrors the attribute layout of res/ecs/text_vs.glsl
// (loc1=uv, loc2=uv2, loc3=color, loc4..7=model, loc8..11=local), so the same
// instanced shader draws every batch.

#include <cstdint>
#include <vector>
#include <unordered_map>

#include <glm/glm.hpp>

namespace gld::ecs {

    struct InstanceData {
        glm::vec4 uv{ 0.f };    // packed atlas corners (see text_vs.glsl)
        glm::vec4 uv2{ 0.f };
        glm::vec4 color{ 1.f };
        glm::mat4 model{ 1.f }; // entity world transform (kept per-instance: a
                                // global batch mixes many parents)
        glm::mat4 local{ 1.f }; // per-glyph/sprite local matrix
    };

    // Grouping key: same texture + same shader program => one batch.
    struct BatchKey {
        const void* atlas = nullptr;  // Texture<D2>* (opaque here)
        const void* shader = nullptr; // Program*
        bool operator==(const BatchKey& o) const { return atlas == o.atlas && shader == o.shader; }
    };
    struct BatchKeyHash {
        std::size_t operator()(const BatchKey& k) const {
            std::size_t a = std::hash<const void*>{}(k.atlas);
            std::size_t b = std::hash<const void*>{}(k.shader);
            return a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2));
        }
    };

    struct Batch {
        std::vector<InstanceData> instances;   // rebuilt every frame
        unsigned int vao = 0;                  // GL: created lazily at render
        unsigned int instance_vbo = 0;
        std::size_t  gpu_cap = 0;              // instances currently allocated on GPU
        std::uint64_t content_hash = 0;
        bool dirty = true;                     // needs GPU re-upload
    };

    struct BatchStore {
        std::unordered_map<BatchKey, Batch, BatchKeyHash> batches;

        // Keep GL handles + capacity; only clear the CPU instance lists.
        void begin_frame() {
            for (auto& [k, b] : batches) b.instances.clear();
        }
        void append(const BatchKey& key, const InstanceData& d) {
            batches[key].instances.push_back(d);
        }
    };

    // FNV-1a over the raw instance bytes; used to skip GPU uploads when unchanged.
    inline std::uint64_t hash_instances(const std::vector<InstanceData>& v) {
        std::uint64_t h = 1469598103934665603ull;
        const auto* p = reinterpret_cast<const unsigned char*>(v.data());
        const std::size_t n = v.size() * sizeof(InstanceData);
        for (std::size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
        return h;
    }

    // Extension point: specialise for each batchable component type.
    //   static Program* shader(EcsWorld&);
    //   static void collect(entt::entity, const C&, const glm::mat4& world,
    //                       BatchStore&, Program* shader);
    template<class C> struct BatchSource;
}
