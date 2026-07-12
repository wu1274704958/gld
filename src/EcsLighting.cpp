#include <ecs/render/Lighting.hpp>

#include <algorithm>
#include <cmath>

#include <ecs/Components.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace gld::ecs {

    namespace {
        void upload_ssbo(GLuint& id, const void* data, std::size_t bytes) {
            if (id == 0) glGenBuffers(1, &id);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
            glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, data, GL_DYNAMIC_DRAW);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }

        glm::vec3 forward_from_world(const glm::mat4& world) {
            return glm::normalize(glm::vec3(world * glm::vec4(0.f, 0.f, -1.f, 0.f)));
        }
    }

    LightingGpuResource::~LightingGpuResource() {
        cleanup();
    }

    void LightingGpuResource::ensure() {
        if (directional_ssbo == 0) glGenBuffers(1, &directional_ssbo);
        if (point_ssbo == 0) glGenBuffers(1, &point_ssbo);
        if (spot_ssbo == 0) glGenBuffers(1, &spot_ssbo);
    }

    void LightingGpuResource::upload() {
        ensure();
        upload_ssbo(directional_ssbo, directional.data(), directional.size() * sizeof(GpuDirectionalLight));
        upload_ssbo(point_ssbo, points.data(), points.size() * sizeof(GpuPointLight));
        upload_ssbo(spot_ssbo, spots.data(), spots.size() * sizeof(GpuSpotLight));
    }

    void LightingGpuResource::bind() const {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, directional_ssbo);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, point_ssbo);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, spot_ssbo);
    }

    void LightingGpuResource::cleanup() {
        if (directional_ssbo != 0) {
            glDeleteBuffers(1, &directional_ssbo);
            directional_ssbo = 0;
        }
        if (point_ssbo != 0) {
            glDeleteBuffers(1, &point_ssbo);
            point_ssbo = 0;
        }
        if (spot_ssbo != 0) {
            glDeleteBuffers(1, &spot_ssbo);
            spot_ssbo = 0;
        }
    }

    void lighting_collect_system(EcsWorld& w) {
        auto& settings = w.resource_or_add<LightingSettings>();
        auto& gpu = w.resource_or_add<LightingGpuResource>();
        auto& reg = w.reg();

        gpu.directional.clear();
        gpu.points.clear();
        gpu.spots.clear();
        gpu.ambient = glm::vec3(0.f);

        for (auto e : reg.view<AmbientLight>()) {
            const auto& l = reg.get<AmbientLight>(e);
            gpu.ambient += l.color * l.intensity;
        }
        if (gpu.ambient == glm::vec3(0.f))
            gpu.ambient = glm::vec3(settings.ambient_strength);

        if (!settings.enabled) {
            gpu.upload();
            return;
        }

        for (auto e : reg.view<DirectionalLight>()) {
            if (gpu.directional.size() >= settings.max_directional) break;
            const auto& l = reg.get<DirectionalLight>(e);
            glm::vec3 dir = glm::normalize(-l.direction);
            gpu.directional.push_back(GpuDirectionalLight{
                glm::vec4(dir, l.intensity),
                glm::vec4(l.color, 1.f)
            });
        }

        auto point_view = reg.view<PointLight, GlobalTransform>();
        for (auto e : point_view) {
            if (gpu.points.size() >= settings.max_point) break;
            const auto& l = point_view.get<PointLight>(e);
            const auto& gt = point_view.get<GlobalTransform>(e);
            glm::vec3 pos = glm::vec3(gt.world[3]);
            gpu.points.push_back(GpuPointLight{
                glm::vec4(pos, l.range),
                glm::vec4(l.color, l.intensity)
            });
        }

        auto spot_view = reg.view<SpotLight, GlobalTransform>();
        for (auto e : spot_view) {
            if (gpu.spots.size() >= settings.max_spot) break;
            const auto& l = spot_view.get<SpotLight>(e);
            const auto& gt = spot_view.get<GlobalTransform>(e);
            glm::vec3 pos = glm::vec3(gt.world[3]);
            glm::vec3 dir = forward_from_world(gt.world);
            gpu.spots.push_back(GpuSpotLight{
                glm::vec4(pos, l.range),
                glm::vec4(dir, std::cos(l.inner_angle)),
                glm::vec4(l.color, l.intensity),
                glm::vec4(std::cos(l.outer_angle), 0.f, 0.f, 0.f)
            });
        }

        gpu.upload();
    }

    void LightingPlugin(App& app) {
        app.world.resource_or_add<LightingSettings>();
        app.world.resource_or_add<LightingGpuResource>();
        app.add_system(Stage::PostUpdate, lighting_collect_system);
    }
}
