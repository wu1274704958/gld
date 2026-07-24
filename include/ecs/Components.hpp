#pragma once

// Core ECS data components (pure data, no logic) + the Time resource.

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <chrono>
#include <utility>

namespace gld::ecs {

    // Local transform (relative to parent).
    struct TransformAccess;

    class Transform {
    public:
        Transform() = default;

        static Transform from_trs(glm::vec3 translation,
                                  glm::vec3 rotation = glm::vec3(0.f),
                                  glm::vec3 scale = glm::vec3(1.f)) {
            Transform result;
            result.translation_ = translation;
            result.rotation_ = rotation;
            result.scale_ = scale;
            return result;
        }

        const glm::vec3& translation() const { return translation_; }
        const glm::vec3& rotation() const { return rotation_; }
        const glm::vec3& scale() const { return scale_; }
        std::uint64_t revision() const { return revision_; }

    private:
        glm::vec3 translation_{0.f};
        glm::vec3 rotation_{0.f};   // euler radians (x,y,z)
        glm::vec3 scale_{1.f};
        std::uint64_t revision_ = 0;

        friend struct TransformAccess;
    };

    struct TransformAccess {
        static glm::vec3& translation(Transform& value) { return value.translation_; }
        static glm::vec3& rotation(Transform& value) { return value.rotation_; }
        static glm::vec3& scale(Transform& value) { return value.scale_; }
        static void revision(Transform& value, std::uint64_t revision) {
            value.revision_ = revision;
        }
    };

    class TransformEditor {
    public:
        explicit TransformEditor(Transform& value) : value_(value) {}

        void set_translation(glm::vec3 value) {
            if (value_.translation() != value) {
                TransformAccess::translation(value_) = value;
                changed_ = true;
            }
        }
        void translate(glm::vec3 delta) { set_translation(value_.translation() + delta); }
        void set_rotation(glm::vec3 value) {
            if (value_.rotation() != value) {
                TransformAccess::rotation(value_) = value;
                changed_ = true;
            }
        }
        void rotate(glm::vec3 delta) { set_rotation(value_.rotation() + delta); }
        void set_scale(glm::vec3 value) {
            if (value_.scale() != value) {
                TransformAccess::scale(value_) = value;
                changed_ = true;
            }
        }
        bool changed() const { return changed_; }
        const Transform& value() const { return value_; }

    private:
        Transform& value_;
        bool changed_ = false;
    };

    // World transform, computed by transform_propagate_system. `version` bumps
    // only when `world` actually changes, so consumers (e.g. the batcher) can do
    // cheap change detection without hashing.
    struct GlobalTransform {
        glm::mat4 world{1.f};
        std::uint32_t version = 0;
    };

    // Hierarchy.
    struct Parent {
        entt::entity value{entt::null};
    };
    struct Children {
        std::vector<entt::entity> value;
    };

    // ---- resources ----
    struct Time {
        float dt = 0.f;
        float raw_dt = 0.f;
        double elapsed = 0.0;
        double wall_elapsed = 0.0;
        std::uint64_t frame = 0;
        float fps = 0.f;
    };

    struct TimeSettings {
        float max_delta = 0.1f;
    };

    struct TimeClock {
        using Clock = std::chrono::steady_clock;
        Clock::time_point previous{};
        bool initialized = false;
    };
}
