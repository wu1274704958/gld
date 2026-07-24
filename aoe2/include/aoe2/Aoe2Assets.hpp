#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <texture.hpp>
#include <ecs/assets/AssetServer.hpp>

namespace gld::ecs::aoe2 {

enum class LayerStatus { Complete, Partial, Missing, Unsupported, Invalid };
enum class AnimationResidencyState { Unloaded, Loading, Ready, Failed };
enum class PlayerColorFormat { None, R8SubcolorAlphaBinary };

enum class AnimationSlot : std::uint16_t {
    Invalid = 0,
    WalkA = 1,
    WalkB = 2,
    WalkC = 3,
    IdleA = 4,
    IdleB = 5,
    IdleC = 6,
    AttackA = 7,
    AttackB = 8,
    AttackC = 9,
    DeathA = 10,
    DecayA = 11,
    ExtensionBegin = 0x100
};

inline constexpr std::uint16_t animation_slot_value(AnimationSlot slot) {
    return static_cast<std::uint16_t>(slot);
}

inline constexpr bool is_extension_animation_slot(AnimationSlot slot) {
    return animation_slot_value(slot) >= animation_slot_value(AnimationSlot::ExtensionBegin);
}

template<std::size_t N>
struct AnimationFixedString {
    char value[N]{};

    constexpr AnimationFixedString(const char (&text)[N]) {
        for (std::size_t i = 0; i < N; ++i) value[i] = text[i];
    }

    constexpr std::string_view view() const { return {value, N - 1}; }
};

template<AnimationSlot Slot, AnimationFixedString Name>
struct AnimationAbiEntry {
    static constexpr AnimationSlot slot = Slot;
    static constexpr auto name = Name;
};

template<class... Entries>
struct AnimationAbiTable {
    struct Record { AnimationSlot slot; std::string_view name; };
    inline static constexpr std::array<Record, sizeof...(Entries)> records{{
        Record{Entries::slot, Entries::name.view()}...
    }};

private:
    static consteval bool valid() {
        for (std::size_t i = 0; i < records.size(); ++i) {
            const auto value = animation_slot_value(records[i].slot);
            if (value == animation_slot_value(AnimationSlot::Invalid) ||
                value >= animation_slot_value(AnimationSlot::ExtensionBegin) ||
                records[i].name.empty()) return false;
            for (std::size_t j = i + 1; j < records.size(); ++j)
                if (records[i].slot == records[j].slot || records[i].name == records[j].name)
                    return false;
        }
        return true;
    }

public:
    static_assert(valid(), "animation ABI contains an invalid/duplicate slot or name");

    static constexpr AnimationSlot find(std::string_view name) {
        for (const auto& record : records)
            if (record.name == name) return record.slot;
        return AnimationSlot::Invalid;
    }

    static constexpr std::string_view name(AnimationSlot slot) {
        for (const auto& record : records)
            if (record.slot == slot) return record.name;
        return {};
    }
};

using DefaultAoe2AnimationAbi = AnimationAbiTable<
    AnimationAbiEntry<AnimationSlot::WalkA, "walkA">,
    AnimationAbiEntry<AnimationSlot::WalkB, "walkB">,
    AnimationAbiEntry<AnimationSlot::WalkC, "walkC">,
    AnimationAbiEntry<AnimationSlot::IdleA, "idleA">,
    AnimationAbiEntry<AnimationSlot::IdleB, "idleB">,
    AnimationAbiEntry<AnimationSlot::IdleC, "idleC">,
    AnimationAbiEntry<AnimationSlot::AttackA, "attackA">,
    AnimationAbiEntry<AnimationSlot::AttackB, "attackB">,
    AnimationAbiEntry<AnimationSlot::AttackC, "attackC">,
    AnimationAbiEntry<AnimationSlot::DeathA, "deathA">,
    AnimationAbiEntry<AnimationSlot::DecayA, "decayA">
>;

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
    static constexpr std::size_t InvalidAnimationIndex = std::numeric_limits<std::size_t>::max();

    std::string id;
    int schema_version = 0;
    PlayerColorFormat player_color_format = PlayerColorFormat::None;
    std::vector<std::string> animation_names;
    std::vector<Animation> animations;
    std::unordered_map<std::string, AnimationSlot> animation_slots_by_name;
    std::vector<std::size_t> slot_to_animation_index;
    std::vector<std::string> extension_animation_names_storage;
    std::vector<std::string> missing_animations;
    std::vector<std::string> warnings;

    void build_animation_slots();
    AnimationSlot find_animation_slot(const std::string& name) const;
    const Animation* animation_at(AnimationSlot slot) const;
    Animation* animation_at(AnimationSlot slot);
    std::string_view animation_name(AnimationSlot slot) const;
    std::size_t extension_animation_count() const { return extension_animation_names_storage.size(); }
    std::span<const std::string> extension_animation_names() const {
        return extension_animation_names_storage;
    }
    bool is_extension_animation(AnimationSlot slot) const {
        return is_extension_animation_slot(slot) && animation_at(slot) != nullptr;
    }
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
