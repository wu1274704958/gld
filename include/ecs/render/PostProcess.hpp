#pragma once

// Graph-backed post-processing. Descriptors are template/static: they implement
// `PostProcessOutput build(PostProcessBuilder&, PostProcessInput) const`.

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "RenderComponents.hpp"
#include "RenderGraph.hpp"
#include "RenderTarget.hpp"
#include "../App.hpp"
#include "../EcsWorld.hpp"

namespace gld::ecs {

    struct PostProcessHandle {
        std::uint64_t value = 0;
        explicit operator bool() const { return value != 0; }
        bool operator==(const PostProcessHandle&) const = default;
    };

    struct PostProcessInput {
        std::shared_ptr<Texture<TexType::D2>> color;
        std::shared_ptr<Texture<TexType::D2>> depth;
        RenderGraphNodeId producer = 0;
        int width = 0;
        int height = 0;
        float near_z = 0.1f;
        float far_z = 200.f;
    };

    using PostProcessOutput = PostProcessInput;

    enum class PostProcessOutputMode {
        Offscreen,
        Final
    };

    struct PostProcessTextureInput {
        std::string uniform;
        PostProcessInput input;
    };

    class PostProcessBuilder {
    public:
        PostProcessBuilder(EcsWorld& world, entt::entity source, std::uint64_t owner,
                           int width, int height, unsigned int final_target,
                           glm::ivec2 final_size, int base_priority, bool final_process,
                           PostProcessInput source_input);

        const PostProcessInput& source_input() const { return source_input_; }

        PostProcessOutput add_pass(
            const std::string& name,
            const std::string& fragment_shader,
            const std::vector<PostProcessTextureInput>& inputs,
            const std::vector<FullscreenUniform>& uniforms = {},
            PostProcessOutputMode mode = PostProcessOutputMode::Offscreen);

        PostProcessOutput add_pass(
            const std::string& name,
            const std::string& fragment_shader,
            const PostProcessInput& input,
            const std::vector<FullscreenUniform>& uniforms = {},
            PostProcessOutputMode mode = PostProcessOutputMode::Offscreen) {
            return add_pass(name, fragment_shader, { PostProcessTextureInput{ "scene", input } }, uniforms, mode);
        }

        PostProcessOutput add_final_pass(
            const std::string& name,
            const std::string& fragment_shader,
            const std::vector<PostProcessTextureInput>& inputs,
            const std::vector<FullscreenUniform>& uniforms = {}) {
            return add_pass(name, fragment_shader, inputs, uniforms,
                            final_process_ ? PostProcessOutputMode::Final : PostProcessOutputMode::Offscreen);
        }

        PostProcessOutput add_final_pass(
            const std::string& name,
            const std::string& fragment_shader,
            const PostProcessInput& input,
            const std::vector<FullscreenUniform>& uniforms = {}) {
            return add_final_pass(name, fragment_shader, { PostProcessTextureInput{ "scene", input } }, uniforms);
        }

        std::vector<entt::entity> created_entities;
        std::vector<std::shared_ptr<RenderTarget>> created_targets;

    private:
        EcsWorld& world_;
        entt::entity source_ = entt::null;
        std::uint64_t owner_ = 0;
        int width_ = 0;
        int height_ = 0;
        unsigned int final_target_ = 0;
        glm::ivec2 final_size_{ 0, 0 };
        int base_priority_ = 0;
        int seq_ = 0;
        bool final_process_ = false;
        PostProcessInput source_input_;
    };

    template<class T>
    concept PostProcessDesc = requires(const T& desc, PostProcessBuilder& builder, PostProcessInput input) {
        { desc.build(builder, input) } -> std::same_as<PostProcessOutput>;
    };

    struct PostProcessManager {
        EcsWorld* world = nullptr;

        template<PostProcessDesc T>
        PostProcessHandle add_post_process(entt::entity source_camera, const T& desc) {
            const PostProcessHandle handle{ next_handle_++ };
            Entry entry;
            entry.handle = handle;
            entry.source = source_camera;
            entry.build = [desc](PostProcessBuilder& builder, PostProcessInput input) {
                return desc.build(builder, input);
            };
            entries_[handle.value] = std::move(entry);
            source_entries_[source_camera].push_back(handle.value);
            rebuild(source_camera);
            return handle;
        }

        void remove(PostProcessHandle handle);
        void clear(entt::entity source_camera);
        void set_enabled(PostProcessHandle handle, bool enabled);
        void update_sizes();
        void cleanup();

    private:
        struct Entry {
            PostProcessHandle handle;
            entt::entity source = entt::null;
            bool enabled = true;
            std::function<PostProcessOutput(PostProcessBuilder&, PostProcessInput)> build;
        };

        struct SourceState {
            bool redirected = false;
            unsigned int original_target = 0;
            glm::ivec2 original_target_size{ 0, 0 };
            std::shared_ptr<RenderTarget> source_target;
            std::vector<entt::entity> generated_entities;
            std::vector<std::shared_ptr<RenderTarget>> generated_targets;
        };

        void rebuild(entt::entity source_camera);
        void clear_generated(entt::entity source_camera, bool restore_source);
        glm::ivec2 target_size_for(entt::entity source_camera) const;

        std::uint64_t next_handle_ = 1;
        std::unordered_map<std::uint64_t, Entry> entries_;
        std::unordered_map<entt::entity, std::vector<std::uint64_t>> source_entries_;
        std::unordered_map<entt::entity, SourceState> sources_;
    };

    struct FogPostProcessDesc {
        glm::vec4 fog_color{ 0.42f, 0.48f, 0.58f, 1.f };
        float fog_start = 10.f;
        float fog_end = 45.f;
        float density = 0.0f;
        float max_amount = 0.92f;

        PostProcessOutput build(PostProcessBuilder& builder, PostProcessInput input) const {
            std::vector<PostProcessTextureInput> inputs{
                PostProcessTextureInput{ "scene", input }
            };
            if (input.depth)
                inputs.push_back(PostProcessTextureInput{ "depthTex", PostProcessInput{
                    input.depth, nullptr, input.producer, input.width, input.height, input.near_z, input.far_z
                } });
            return builder.add_final_pass("fog", "ecs/post_fog_fg.glsl", inputs, {
                FullscreenUniform{ "fogColor", fog_color },
                FullscreenUniform{ "fogStart", fog_start },
                FullscreenUniform{ "fogEnd", fog_end },
                FullscreenUniform{ "density", density },
                FullscreenUniform{ "maxAmount", max_amount },
                FullscreenUniform{ "nearZ", input.near_z },
                FullscreenUniform{ "farZ", input.far_z },
                FullscreenUniform{ "hasDepth", input.depth ? 1 : 0 },
            });
        }
    };

    struct BloomPostProcessDesc {
        float threshold = 0.9f;
        float knee = 0.35f;
        float intensity = 0.8f;
        float exposure = 1.0f;

        PostProcessOutput build(PostProcessBuilder& builder, PostProcessInput input) const {
            auto bright = builder.add_pass("bloom_bright", "bloom/bright_fg.glsl", input, {
                FullscreenUniform{ "threshold", threshold },
                FullscreenUniform{ "knee", knee },
            });
            auto blur_h = builder.add_pass("bloom_blur_h", "bloom/blur_fg.glsl",
                { PostProcessTextureInput{ "image", bright } },
                {
                    FullscreenUniform{ "horizontal", 1 },
                    FullscreenUniform{ "texel", glm::vec2(1.f / static_cast<float>(bright.width), 1.f / static_cast<float>(bright.height)) },
                });
            auto blur_v = builder.add_pass("bloom_blur_v", "bloom/blur_fg.glsl",
                { PostProcessTextureInput{ "image", blur_h } },
                {
                    FullscreenUniform{ "horizontal", 0 },
                    FullscreenUniform{ "texel", glm::vec2(1.f / static_cast<float>(blur_h.width), 1.f / static_cast<float>(blur_h.height)) },
                });
            return builder.add_final_pass("bloom_composite", "bloom/composite_fg.glsl",
                {
                    PostProcessTextureInput{ "scene", input },
                    PostProcessTextureInput{ "bloomBlur", blur_v },
                },
                {
                    FullscreenUniform{ "intensity", intensity },
                    FullscreenUniform{ "exposure", exposure },
                });
        }
    };

    struct FogBloomPostProcessDesc {
        glm::vec4 fog_color{ 0.28f, 0.34f, 0.45f, 1.f };
        float fog_start = 7.f;
        float fog_end = 38.f;
        float fog_density = 0.018f;
        float fog_max_amount = 0.96f;

        float bloom_threshold = 0.62f;
        float bloom_knee = 0.28f;
        float bloom_intensity = 2.2f;
        float exposure = 1.10f;
        float bloom_fog_attenuation = 0.55f;

        PostProcessOutput build(PostProcessBuilder& builder, PostProcessInput input) const {
            const PostProcessInput& source = builder.source_input();

            std::vector<PostProcessTextureInput> fog_inputs{
                PostProcessTextureInput{ "scene", source }
            };
            if (source.depth) {
                fog_inputs.push_back(PostProcessTextureInput{ "depthTex", PostProcessInput{
                    source.depth, nullptr, source.producer, source.width, source.height, source.near_z, source.far_z
                } });
            }

            auto fogged = builder.add_pass("dag_fog", "ecs/post_fog_fg.glsl", fog_inputs, {
                FullscreenUniform{ "fogColor", fog_color },
                FullscreenUniform{ "fogStart", fog_start },
                FullscreenUniform{ "fogEnd", fog_end },
                FullscreenUniform{ "density", fog_density },
                FullscreenUniform{ "maxAmount", fog_max_amount },
                FullscreenUniform{ "nearZ", source.near_z },
                FullscreenUniform{ "farZ", source.far_z },
                FullscreenUniform{ "hasDepth", source.depth ? 1 : 0 },
            });

            auto bright = builder.add_pass("dag_bloom_bright", "bloom/bright_fg.glsl", source, {
                FullscreenUniform{ "threshold", bloom_threshold },
                FullscreenUniform{ "knee", bloom_knee },
            });
            auto blur_h = builder.add_pass("dag_bloom_blur_h", "bloom/blur_fg.glsl",
                { PostProcessTextureInput{ "image", bright } },
                {
                    FullscreenUniform{ "horizontal", 1 },
                    FullscreenUniform{ "texel", glm::vec2(1.f / static_cast<float>(bright.width), 1.f / static_cast<float>(bright.height)) },
                });
            auto blur_v = builder.add_pass("dag_bloom_blur_v", "bloom/blur_fg.glsl",
                { PostProcessTextureInput{ "image", blur_h } },
                {
                    FullscreenUniform{ "horizontal", 0 },
                    FullscreenUniform{ "texel", glm::vec2(1.f / static_cast<float>(blur_h.width), 1.f / static_cast<float>(blur_h.height)) },
                });

            std::vector<PostProcessTextureInput> composite_inputs{
                PostProcessTextureInput{ "scene", fogged },
                PostProcessTextureInput{ "bloomBlur", blur_v }
            };
            if (source.depth) {
                composite_inputs.push_back(PostProcessTextureInput{ "depthTex", PostProcessInput{
                    source.depth, nullptr, source.producer, source.width, source.height, source.near_z, source.far_z
                } });
            }

            return builder.add_final_pass("dag_fog_bloom_composite", "bloom/composite_fg.glsl",
                composite_inputs,
                {
                    FullscreenUniform{ "intensity", bloom_intensity },
                    FullscreenUniform{ "exposure", exposure },
                    FullscreenUniform{ "bloomFogAttenuation", bloom_fog_attenuation },
                    FullscreenUniform{ "fogStart", fog_start },
                    FullscreenUniform{ "fogEnd", fog_end },
                    FullscreenUniform{ "density", fog_density },
                    FullscreenUniform{ "maxAmount", fog_max_amount },
                    FullscreenUniform{ "nearZ", source.near_z },
                    FullscreenUniform{ "farZ", source.far_z },
                    FullscreenUniform{ "hasDepth", source.depth ? 1 : 0 },
                });
        }
    };

    void post_process_update_system(EcsWorld& w);
    void PostProcessPlugin(App& app);
}
