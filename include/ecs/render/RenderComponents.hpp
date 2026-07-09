#pragma once

// ECS render + demo components.

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
    };

    struct Visibility { bool visible = true; };

    struct AutoRotate { glm::vec3 speed{ 0.f }; }; // radians/sec

    // Camera resource. Orbit params drive the view; projection from window size.
    struct Camera {
        glm::mat4 projection{ 1.f };
        glm::mat4 view{ 1.f };
        glm::vec3 target{ 0.f };
        float dist = 6.f;
        float yaw = 0.6f;
        float pitch = 0.4f;
        float fov = 60.f;
    };
}
