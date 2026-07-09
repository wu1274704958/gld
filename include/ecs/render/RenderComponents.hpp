#pragma once

// ECS render + demo components.

#include <cstdint>
#include <memory>
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

    // Optional drawable layer membership. A camera renders an entity when
    // (camera.layers & RenderLayer.mask) != 0. Entities without RenderLayer are
    // treated as mask 0x1 (layer 0).
    struct RenderLayer { std::uint32_t mask = 0x1u; };

    // Inserted by render_system on the primary window camera after commands are
    // submitted; present_system waits on the fence and swaps. `fence` is a GLsync
    // (opaque here to keep GL out of the header).
    struct WaitPresent { void* fence = nullptr; };
}
