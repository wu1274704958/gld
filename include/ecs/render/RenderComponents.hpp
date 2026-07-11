#pragma once

// ECS render + demo components.

#include <cstdint>
#include <memory>
#include <string>
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

    inline constexpr std::uint32_t RenderPassMesh  = 0x1u;
    inline constexpr std::uint32_t RenderPassBatch = 0x2u;
    inline constexpr std::uint32_t RenderPassFullscreen = 0x4u;

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
        // 0 keeps the legacy default: Perspective -> mesh, Ortho -> batch.
        // Set RenderPassMesh | RenderPassBatch for mixed cameras.
        std::uint32_t pass_mask = 0;
        bool do_clear = true;
        glm::vec4 clear_color{ 0.055f, 0.05f, 0.075f, 1.f };

        float fov = 60.f, near_z = 0.1f, far_z = 200.f;  // perspective params

        glm::mat4 view{ 1.f };        // set by user/orbit systems (perspective)
        glm::mat4 projection{ 1.f };  // computed by camera_matrices_system
    };

    inline std::uint32_t effective_pass_mask(const Camera& c) {
        if (c.pass_mask != 0) return c.pass_mask;
        return c.kind == CameraKind::Perspective ? RenderPassMesh : RenderPassBatch;
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
