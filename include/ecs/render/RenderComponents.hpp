#pragma once

// ECS render + demo components.

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vertex_arr.hpp>
#include <program.hpp>
#include <texture.hpp>
#include "../assets/Handle.hpp"

namespace gld::ecs {

    // A drawable mesh (VAO + index count). mode is a GL enum (0x0004 = GL_TRIANGLES).
    struct MeshHandle {
        std::shared_ptr<VertexArr> vao;
        int index_count = 0;
        unsigned int mode = 0x0004; // GL_TRIANGLES
    };

    // Shader + optional texture + tint. Assets referenced by Handle (may still
    // be loading; the render system skips entities whose shader isn't ready).
    struct Material {
        Handle<Program> shader;
        Handle<Texture<TexType::D2>> texture;
        glm::vec4 color{ 1.f };
        // Optional non-asset texture (e.g. an offscreen render-target colour).
        // When set it overrides `texture`.
        std::shared_ptr<Texture<TexType::D2>> tex_override;
    };

    struct Visibility { bool visible = true; };

    struct AutoRotate { glm::vec3 speed{ 0.f }; }; // radians/sec

    // ---- multi-camera rendering ----

    enum class CameraKind { Perspective, Ortho };

    enum class RenderStateValue { Auto, Enabled, Disabled };

    enum class BlendFactor : unsigned int {
        Zero = GL_ZERO,
        One = GL_ONE,
        SrcAlpha = GL_SRC_ALPHA,
        OneMinusSrcAlpha = GL_ONE_MINUS_SRC_ALPHA,
        SrcColor = GL_SRC_COLOR,
        OneMinusSrcColor = GL_ONE_MINUS_SRC_COLOR,
        DstAlpha = GL_DST_ALPHA,
        OneMinusDstAlpha = GL_ONE_MINUS_DST_ALPHA,
        DstColor = GL_DST_COLOR,
        OneMinusDstColor = GL_ONE_MINUS_DST_COLOR
    };

    enum class BlendEquation : unsigned int {
        Add = GL_FUNC_ADD,
        Subtract = GL_FUNC_SUBTRACT,
        ReverseSubtract = GL_FUNC_REVERSE_SUBTRACT,
        Min = GL_MIN,
        Max = GL_MAX
    };

    enum class StencilFunc : unsigned int {
        Never = GL_NEVER,
        Less = GL_LESS,
        LessEqual = GL_LEQUAL,
        Greater = GL_GREATER,
        GreaterEqual = GL_GEQUAL,
        Equal = GL_EQUAL,
        NotEqual = GL_NOTEQUAL,
        Always = GL_ALWAYS
    };

    enum class StencilOp : unsigned int {
        Keep = GL_KEEP,
        Zero = GL_ZERO,
        Replace = GL_REPLACE,
        Increment = GL_INCR,
        IncrementWrap = GL_INCR_WRAP,
        Decrement = GL_DECR,
        DecrementWrap = GL_DECR_WRAP,
        Invert = GL_INVERT
    };

    struct StencilFaceState {
        StencilFunc func = StencilFunc::Always;
        int ref = 0;
        unsigned int mask = 0xFFu;
        StencilOp fail = StencilOp::Keep;
        StencilOp depth_fail = StencilOp::Keep;
        StencilOp pass = StencilOp::Keep;
    };

    struct RenderPassState {
        RenderStateValue depth_test = RenderStateValue::Auto;
        RenderStateValue depth_write = RenderStateValue::Auto;
        RenderStateValue blend = RenderStateValue::Auto;
        BlendFactor blend_src = BlendFactor::SrcAlpha;
        BlendFactor blend_dst = BlendFactor::OneMinusSrcAlpha;
        BlendEquation blend_equation = BlendEquation::Add;
        RenderStateValue stencil = RenderStateValue::Auto;
        StencilFaceState stencil_state;
    };

    struct ResolvedRenderPassState {
        bool depth_test = false;
        bool depth_write = false;
        bool blend = false;
        BlendFactor blend_src = BlendFactor::SrcAlpha;
        BlendFactor blend_dst = BlendFactor::OneMinusSrcAlpha;
        BlendEquation blend_equation = BlendEquation::Add;
        bool stencil = false;
        StencilFaceState stencil_state;
    };

    inline constexpr std::uint32_t RenderPassMesh  = 0x1u;
    inline constexpr std::uint32_t RenderPassBatch = 0x2u;
    inline constexpr std::uint32_t RenderPassFullscreen = 0x4u;

    template<std::uint32_t PassId>
    struct RenderPassT {
        static constexpr std::uint32_t id = PassId;
        RenderPassState state;
    };

    using MeshPass = RenderPassT<RenderPassMesh>;
    using BatchPass = RenderPassT<RenderPassBatch>;
    using FullscreenRenderPass = RenderPassT<RenderPassFullscreen>;

    template<class T>
    concept IRenderPass = requires(T pass) {
        { T::id } -> std::convertible_to<std::uint32_t>;
        { pass.state } -> std::same_as<RenderPassState&>;
    };

    template<IRenderPass... Passes>
    struct RenderPasses {
        static_assert(sizeof...(Passes) > 0, "RenderPasses requires at least one pass");
        std::tuple<Passes...> passes;

        template<class Pass>
        Pass* get() {
            return &std::get<Pass>(passes);
        }

        template<class Pass>
        const Pass* get() const {
            return &std::get<Pass>(passes);
        }
    };

    template<class T>
    struct IsRenderPasses : std::false_type {};

    template<IRenderPass... Passes>
    struct IsRenderPasses<RenderPasses<Passes...>> : std::true_type {};

    template<class T>
    concept IRenderPassComponent = IsRenderPasses<std::remove_cvref_t<T>>::value;

    // Camera is a COMPONENT (many allowed), not a resource. Each camera is a
    // render pass: it renders the drawables its `layers` mask selects, into
    // `target` (0 = window default framebuffer, else a custom FBO), and passes
    // are ordered by `priority` (smaller first -> offscreen deps render earlier).
    struct Camera {
        int id = -1;                              // assigned by spawn_camera_system
        CameraKind kind = CameraKind::Perspective;
        int priority = 0;                         // ascending render order
        unsigned int target = 0;                  // FBO id; 0 = window default
        glm::ivec2 target_size{ 0, 0 };           // offscreen size; (0,0)=follow window
        std::uint32_t layers = 0xFFFFFFFFu;       // culling mask
        bool do_clear = true;
        glm::vec4 clear_color{ 0.055f, 0.05f, 0.075f, 1.f };

        float fov = 60.f, near_z = 0.1f, far_z = 200.f;  // perspective params

        glm::mat4 view{ 1.f };        // set by user/orbit systems (perspective)
        glm::mat4 projection{ 1.f };  // computed by camera_matrices_system
    };

    inline bool resolve_state_value(RenderStateValue value, bool auto_value) {
        switch (value) {
        case RenderStateValue::Enabled: return true;
        case RenderStateValue::Disabled: return false;
        case RenderStateValue::Auto:
        default: return auto_value;
        }
    }

    inline ResolvedRenderPassState resolve_render_pass_state(std::uint32_t pass_id, const RenderPassState& state) {
        bool default_depth_test = false;
        bool default_depth_write = false;
        bool default_blend = false;

        switch (pass_id) {
        case RenderPassMesh:
            default_depth_test = true;
            default_depth_write = true;
            default_blend = false;
            break;
        case RenderPassBatch:
            default_depth_test = false;
            default_depth_write = false;
            default_blend = true;
            break;
        case RenderPassFullscreen:
            default_depth_test = false;
            default_depth_write = false;
            default_blend = false;
            break;
        default:
            break;
        }

        return {
            resolve_state_value(state.depth_test, default_depth_test),
            resolve_state_value(state.depth_write, default_depth_write),
            resolve_state_value(state.blend, default_blend),
            state.blend_src,
            state.blend_dst,
            state.blend_equation,
            resolve_state_value(state.stencil, false),
            state.stencil_state
        };
    }

    // Optional drawable layer membership. A camera renders an entity when
    // (camera.layers & RenderLayer.mask) != 0. Entities without RenderLayer are
    // treated as mask 0x1 (layer 0).
    struct RenderLayer { std::uint32_t mask = 0x1u; };

    // Inserted by render_system on the primary window camera after commands are
    // submitted; present_system waits on the fence and swaps. `fence` is a GLsync
    // (opaque here to keep GL out of the header).
    struct WaitPresent { void* fence = nullptr; };

    using FullscreenUniformValue = std::variant<int, float, glm::vec2, glm::vec3, glm::vec4>;

    struct FullscreenTextureSlot {
        std::string uniform;
        std::shared_ptr<Texture<TexType::D2>> texture;
    };

    struct FullscreenUniform {
        std::string name;
        FullscreenUniformValue value;
    };

    struct FullscreenPass {
        Handle<Program> shader;
        std::vector<FullscreenTextureSlot> textures;
        std::vector<FullscreenUniform> uniforms;
        std::string debug_name;
    };

    struct FullscreenResources {
        FullscreenResources() = default;
        ~FullscreenResources();
        FullscreenResources(const FullscreenResources&) = delete;
        FullscreenResources& operator=(const FullscreenResources&) = delete;

        unsigned int vao = 0;
        unsigned int vbo = 0;
        unsigned int ebo = 0;
        int index_count = 6;
        bool ready = false;
    };

    void destroy_fullscreen_resources_gpu(FullscreenResources& res);

    struct RenderSettings {
        // Debug/explicit sync mode. Default presentation avoids a per-frame
        // glClientWaitSync stall and lets glfwSwapBuffers/vsync provide pacing.
        bool wait_for_present_fence = false;
    };

    struct RenderDiagnostics {
        std::uint64_t frame = 0;
        std::uint32_t cameras = 0;
        std::uint32_t passes = 0;
        std::uint32_t mesh_draws = 0;
        std::uint32_t batch_draws = 0;
        std::uint32_t batch_instances = 0;
        std::uint32_t batch_uploads = 0;
        std::uint32_t batch_groups = 0;
        std::uint32_t batch_groups_destroyed = 0;
        std::uint64_t batch_upload_bytes = 0;
        std::uint32_t mesh_skipped_unloaded = 0;
        std::uint32_t mesh_skipped_invalid = 0;
        std::uint32_t present_swaps = 0;
        std::uint32_t present_fence_waits = 0;
        std::uint32_t graph_nodes = 0;
        std::uint32_t graph_edges = 0;
        std::uint32_t graph_executed = 0;
        std::uint32_t graph_cycles = 0;
        std::uint32_t graph_missing_cameras = 0;
        std::uint32_t graph_skipped_invalid = 0;

        void begin_frame() {
            ++frame;
            cameras = 0;
            passes = 0;
            mesh_draws = 0;
            batch_draws = 0;
            batch_instances = 0;
            batch_uploads = 0;
            batch_groups = 0;
            batch_groups_destroyed = 0;
            batch_upload_bytes = 0;
            mesh_skipped_unloaded = 0;
            mesh_skipped_invalid = 0;
            present_swaps = 0;
            present_fence_waits = 0;
            graph_nodes = 0;
            graph_edges = 0;
            graph_executed = 0;
            graph_cycles = 0;
            graph_missing_cameras = 0;
            graph_skipped_invalid = 0;
        }
    };
}
