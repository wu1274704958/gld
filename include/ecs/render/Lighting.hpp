#pragma once

// ECS lighting components and GPU resources.
//
// Lights are regular ECS components. The lighting system collects them after
// transforms are propagated and uploads packed arrays to SSBOs consumed by the
// mesh pass shader.

#include <cstdint>
#include <vector>

#include <glad/glad.h>
#include <glm/glm.hpp>

#include "../App.hpp"
#include "../EcsWorld.hpp"

namespace gld::ecs {

    struct DirectionalLight {
        glm::vec3 direction{ -0.3f, -1.0f, -0.2f };
        glm::vec3 color{ 1.0f };
        float intensity = 1.0f;
    };

    struct AmbientLight {
        glm::vec3 color{ 1.0f };
        float intensity = 0.18f;
    };

    struct PointLight {
        glm::vec3 color{ 1.0f };
        float intensity = 1.0f;
        float range = 10.0f;
    };

    struct SpotLight {
        glm::vec3 color{ 1.0f };
        float intensity = 1.0f;
        float range = 15.0f;
        float inner_angle = 0.35f; // radians
        float outer_angle = 0.60f; // radians
    };

    struct LightLayer {
        std::uint32_t mask = 0xFFFFFFFFu;
    };

    struct LightingSettings {
        bool enabled = true;
        float ambient_strength = 0.18f;
        std::uint32_t max_directional = 32;
        std::uint32_t max_point = 512;
        std::uint32_t max_spot = 256;
    };

    struct GpuDirectionalLight {
        glm::vec4 direction_intensity{ 0.f }; // xyz = direction to light, w = intensity
        glm::vec4 color{ 1.f };
    };

    struct GpuPointLight {
        glm::vec4 position_range{ 0.f };      // xyz = world position, w = range
        glm::vec4 color_intensity{ 1.f };     // rgb = color, w = intensity
    };

    struct GpuSpotLight {
        glm::vec4 position_range{ 0.f };      // xyz = world position, w = range
        glm::vec4 direction_angles{ 0.f };    // xyz = direction to target, w = cos(inner)
        glm::vec4 color_intensity{ 1.f };     // rgb = color, w = intensity
        glm::vec4 params{ 0.f };              // x = cos(outer)
    };

    struct LightingGpuResource {
        LightingGpuResource() = default;
        ~LightingGpuResource();
        LightingGpuResource(const LightingGpuResource&) = delete;
        LightingGpuResource& operator=(const LightingGpuResource&) = delete;

        GLuint directional_ssbo = 0;
        GLuint point_ssbo = 0;
        GLuint spot_ssbo = 0;

        std::vector<GpuDirectionalLight> directional;
        std::vector<GpuPointLight> points;
        std::vector<GpuSpotLight> spots;
        glm::vec3 ambient{ 0.18f };

        void ensure();
        void upload();
        void bind() const;
        void cleanup();
    };

    void lighting_collect_system(EcsWorld& w);
    void LightingPlugin(App& app);
}
