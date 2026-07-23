#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <texture.hpp>
#include <ecs/assets/AssetServer.hpp>

namespace gld::ecs::aoe2 {

enum class LayerStatus { Complete, Partial, Missing, Unsupported, Invalid };
enum class AnimationResidencyState { Unloaded, Loading, Ready, Failed };

struct Frame {
    int source_ordinal = -1;
    int source_frame_index = -1;
    int direction = 0;
    int frame = 0;
    bool present = false;
    int x = 0, y = 0, width = 0, height = 0;
    glm::vec2 foot{0.f};
    glm::vec4 uv{0.f};
};

struct Layer {
    LayerStatus status = LayerStatus::Missing;
    std::string image_path;
    int atlas_width = 0;
    int atlas_height = 0;
    std::vector<Frame> frames;
    Handle<Texture<TexType::D2>> texture;
    bool usable() const { return status == LayerStatus::Complete || status == LayerStatus::Partial; }
};

struct Animation {
    std::string name;
    float fps = 30.f;
    int direction_count = 0;
    int frames_per_direction = 0;
    Layer main;
    Layer shadow;
    Layer player_color;
    std::vector<std::string> warnings;
    AnimationResidencyState residency = AnimationResidencyState::Unloaded;
    std::string residency_error;
    std::uint64_t residency_revision = 0;

    const Frame* get(const Layer& layer, int direction, int frame) const;
};

struct Aoe2UnitAppearance {
    std::string id;
    int schema_version = 0;
    std::string player_color_format;
    std::vector<std::string> animation_names;
    std::unordered_map<std::string, Animation> animations;
    std::vector<std::string> missing_animations;
    std::vector<std::string> warnings;

    const Animation* find_animation(const std::string& name) const;
    Animation* find_animation(const std::string& name);
};

struct Aoe2UnitAppearanceDesc
    : BaseAssetDesc<Aoe2UnitAppearanceDesc, std::string> {
    using Asset = Aoe2UnitAppearance;
    using BaseAssetDesc::BaseAssetDesc;
    const std::string& manifest_path() const { return get<0>(); }
};

struct Aoe2UnitAppearanceLoader : IAssetLoader<Aoe2UnitAppearanceDesc> {
    explicit Aoe2UnitAppearanceLoader(AssetServer&) {}
    std::shared_ptr<void> load_cpu(const Aoe2UnitAppearanceDesc&, const IFileSystem&) override;
    std::shared_ptr<Aoe2UnitAppearance> finalize(std::shared_ptr<void>, const Aoe2UnitAppearanceDesc&) override;
};

} // namespace gld::ecs::aoe2
