#include <aoe2/Aoe2Assets.hpp>

#include <algorithm>
#include <filesystem>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <nlohmann/json.hpp>

namespace gld::ecs::aoe2 {
namespace {
using json = nlohmann::json;

LayerStatus parse_status(const std::string& value) {
    if (value == "complete") return LayerStatus::Complete;
    if (value == "partial") return LayerStatus::Partial;
    if (value == "missing") return LayerStatus::Missing;
    if (value == "unsupported") return LayerStatus::Unsupported;
    if (value == "invalid") return LayerStatus::Invalid;
    throw std::runtime_error("unknown layer status: " + value);
}

std::string join_from(const std::string& file, const std::string& relative) {
    return (std::filesystem::path(file).parent_path() / relative).generic_string();
}

Layer parse_layer(const json& value, const std::string& config_path) {
    Layer layer;
    layer.status = parse_status(value.at("status").get<std::string>());
    if (!layer.usable()) return layer;
    layer.atlas_width = value.at("atlas_w").get<int>();
    layer.atlas_height = value.at("atlas_h").get<int>();
    if (layer.atlas_width <= 0 || layer.atlas_height <= 0)
        throw std::runtime_error("invalid atlas dimensions");
    layer.image_path = join_from(config_path, value.at("image").get<std::string>());
    for (const auto& f : value.at("frames")) {
        Frame frame;
        frame.source_ordinal = f.at("source_ordinal").get<int>();
        frame.source_frame_index = f.at("source_frame_index").get<int>();
        frame.direction = f.at("direction").get<int>();
        frame.frame = f.at("frame").get<int>();
        frame.present = f.at("present").get<bool>();
        frame.x = f.at("x").get<int>();
        frame.y = f.at("y").get<int>();
        frame.width = f.at("w").get<int>();
        frame.height = f.at("h").get<int>();
        const auto& foot = f.at("foot");
        if (foot.at("space").get<std::string>() != "frame_pixels_top_left")
            throw std::runtime_error("unsupported foot coordinate space");
        frame.foot = {foot.at("x").get<float>(), foot.at("y").get<float>()};
        if (frame.x < 0 || frame.y < 0 || frame.width <= 0 || frame.height <= 0 ||
            frame.x + frame.width > layer.atlas_width || frame.y + frame.height > layer.atlas_height)
            throw std::runtime_error("frame rectangle is outside atlas");
        frame.uv = {
            static_cast<float>(frame.x) / layer.atlas_width,
            static_cast<float>(frame.y) / layer.atlas_height,
            static_cast<float>(frame.width) / layer.atlas_width,
            static_cast<float>(frame.height) / layer.atlas_height
        };
        layer.frames.push_back(frame);
    }
    return layer;
}

void validate_layer_frames(const Layer& layer, const Animation& animation, const char* name) {
    if (!layer.usable()) return;
    const std::size_t expected = static_cast<std::size_t>(animation.direction_count) * animation.frames_per_direction;
    if (layer.frames.size() != expected)
        throw std::runtime_error(std::string(name) + " frame count mismatch");
    for (std::size_t i = 0; i < layer.frames.size(); ++i) {
        const auto& f = layer.frames[i];
        const int expected_direction = static_cast<int>(i) / animation.frames_per_direction;
        const int expected_frame = static_cast<int>(i) % animation.frames_per_direction;
        if (f.direction != expected_direction || f.frame != expected_frame)
            throw std::runtime_error(std::string(name) + " is not direction-major");
    }
}

void validate_player_layout(const Layer& main, const Layer& mask) {
    if (!mask.usable()) return;
    if (main.atlas_width != mask.atlas_width || main.atlas_height != mask.atlas_height ||
        main.frames.size() != mask.frames.size())
        throw std::runtime_error("player-color atlas layout differs from main");
    for (std::size_t i = 0; i < main.frames.size(); ++i) {
        const auto& a = main.frames[i];
        const auto& b = mask.frames[i];
        if (a.x != b.x || a.y != b.y || a.width != b.width || a.height != b.height ||
            a.direction != b.direction || a.frame != b.frame ||
            a.source_ordinal != b.source_ordinal || a.foot != b.foot)
            throw std::runtime_error("player-color frame layout differs from main");
    }
}
} // namespace

const Frame* Animation::get(const Layer& layer, int direction, int frame) const {
    if (!layer.usable() || direction < 0 || direction >= direction_count ||
        frame < 0 || frame >= frames_per_direction) return nullptr;
    const std::size_t index = static_cast<std::size_t>(direction) * frames_per_direction + frame;
    return index < layer.frames.size() ? &layer.frames[index] : nullptr;
}

void Aoe2UnitAppearance::build_animation_slots() {
    std::sort(animations.begin(), animations.end(), [](const Animation& lhs, const Animation& rhs) {
        return lhs.name < rhs.name;
    });
    animation_names.clear();
    animation_slots_by_name.clear();
    extension_animation_names_storage.clear();

    std::vector<std::size_t> extension_indices;
    animation_names.reserve(animations.size());
    animation_slots_by_name.reserve(animations.size());
    for (std::size_t index = 0; index < animations.size(); ++index) {
        const auto& name = animations[index].name;
        if (!animation_names.empty() && animation_names.back() == name)
            throw std::runtime_error("duplicate animation name: " + name);
        animation_names.push_back(name);
        const auto core_slot = DefaultAoe2AnimationAbi::find(name);
        if (core_slot == AnimationSlot::Invalid) extension_indices.push_back(index);
        else animation_slots_by_name.emplace(name, core_slot);
    }

    const auto extension_begin = static_cast<std::size_t>(
        animation_slot_value(AnimationSlot::ExtensionBegin));
    const auto max_slot_count = static_cast<std::size_t>(
        std::numeric_limits<std::uint16_t>::max()) + 1u;
    if (extension_indices.size() > max_slot_count - extension_begin)
        throw std::runtime_error("too many extension animations");

    slot_to_animation_index.assign(extension_begin + extension_indices.size(), InvalidAnimationIndex);
    for (std::size_t index = 0; index < animations.size(); ++index) {
        const auto slot = DefaultAoe2AnimationAbi::find(animations[index].name);
        if (slot != AnimationSlot::Invalid)
            slot_to_animation_index[animation_slot_value(slot)] = index;
    }
    extension_animation_names_storage.reserve(extension_indices.size());
    for (std::size_t extension = 0; extension < extension_indices.size(); ++extension) {
        const auto animation_index = extension_indices[extension];
        const auto raw_slot = static_cast<std::uint16_t>(extension_begin + extension);
        const auto slot = static_cast<AnimationSlot>(raw_slot);
        const auto& name = animations[animation_index].name;
        slot_to_animation_index[raw_slot] = animation_index;
        animation_slots_by_name.emplace(name, slot);
        extension_animation_names_storage.push_back(name);
    }
}

AnimationSlot Aoe2UnitAppearance::find_animation_slot(const std::string& name) const {
    const auto it = animation_slots_by_name.find(name);
    return it == animation_slots_by_name.end() ? AnimationSlot::Invalid : it->second;
}

const Animation* Aoe2UnitAppearance::animation_at(AnimationSlot slot) const {
    const auto raw = static_cast<std::size_t>(animation_slot_value(slot));
    if (slot == AnimationSlot::Invalid || raw >= slot_to_animation_index.size()) return nullptr;
    const auto index = slot_to_animation_index[raw];
    return index < animations.size() ? &animations[index] : nullptr;
}

Animation* Aoe2UnitAppearance::animation_at(AnimationSlot slot) {
    return const_cast<Animation*>(std::as_const(*this).animation_at(slot));
}

std::string_view Aoe2UnitAppearance::animation_name(AnimationSlot slot) const {
    const auto* animation = animation_at(slot);
    return animation ? std::string_view(animation->name) : std::string_view{};
}

const Animation* Aoe2UnitAppearance::find_animation(const std::string& name) const {
    return animation_at(find_animation_slot(name));
}

Animation* Aoe2UnitAppearance::find_animation(const std::string& name) {
    return animation_at(find_animation_slot(name));
}

std::shared_ptr<void> Aoe2UnitAppearanceLoader::load_cpu(
    const Aoe2UnitAppearanceDesc& desc, const IFileSystem& fs) {
    try {
        const auto manifest_text = fs.read_text(desc.manifest_path());
        if (!manifest_text) return nullptr;
        const json manifest = json::parse(*manifest_text);
        if (manifest.at("schema_version").get<int>() != 2 ||
            manifest.at("kind").get<std::string>() != "aoe2de_unit") return nullptr;

        auto appearance = std::make_shared<Aoe2UnitAppearance>();
        appearance->schema_version = 2;
        appearance->id = manifest.at("id").get<std::string>();
        const auto player_color_format = manifest.at("export_settings")
            .at("player_color").at("format").get<std::string>();
        if (player_color_format != "r8_subcolor_alpha_binary")
            throw std::runtime_error("unsupported player-color format: " + player_color_format);
        appearance->player_color_format = PlayerColorFormat::R8SubcolorAlphaBinary;
        if (manifest.contains("missing_animations"))
            appearance->missing_animations = manifest.at("missing_animations").get<std::vector<std::string>>();

        const auto& records = manifest.at("animations");
        for (auto it = records.begin(); it != records.end(); ++it) {
            if (it.value().at("status").get<std::string>() != "exported") continue;
            const std::string name = it.key();
            const std::string config_path = join_from(desc.manifest_path(), it.value().at("config").get<std::string>());
            const auto config_text = fs.read_text(config_path);
            if (!config_text) throw std::runtime_error("cannot read " + config_path);
            const json config = json::parse(*config_text);
            if (config.at("schema_version").get<int>() != 2 ||
                config.at("frame_order").get<std::string>() != "direction_major")
                throw std::runtime_error("unsupported animation schema/order: " + name);

            Animation animation;
            animation.name = name;
            animation.fps = config.at("fps").get<float>();
            animation.direction_count = config.at("direction_count").get<int>();
            animation.frames_per_direction = config.at("frames_per_direction").get<int>();
            if (!(animation.fps > 0.f) || animation.direction_count <= 0 || animation.frames_per_direction <= 0)
                throw std::runtime_error("invalid animation timing/dimensions: " + name);
            const auto& layers = config.at("layers");
            animation.main = parse_layer(layers.at("main"), config_path);
            animation.shadow = parse_layer(layers.at("shadow"), config_path);
            animation.player_color = parse_layer(layers.at("player_color"), config_path);
            validate_layer_frames(animation.main, animation, "main");
            validate_layer_frames(animation.shadow, animation, "shadow");
            validate_layer_frames(animation.player_color, animation, "player_color");
            validate_player_layout(animation.main, animation.player_color);
            if (config.contains("warnings")) {
                for (const auto& warning : config.at("warnings"))
                    animation.warnings.push_back(warning.value("message", std::string{}));
            }
            appearance->animations.push_back(std::move(animation));
        }
        if (appearance->animations.empty()) return nullptr;
        appearance->build_animation_slots();
        return appearance;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[aoe2] failed to parse %s: %s\n",
                     desc.manifest_path().c_str(), error.what());
        return nullptr;
    }
}

std::shared_ptr<Aoe2UnitAppearance> Aoe2UnitAppearanceLoader::finalize(
    std::shared_ptr<void> cpu, const Aoe2UnitAppearanceDesc&) {
    auto appearance = std::static_pointer_cast<Aoe2UnitAppearance>(cpu);
    return appearance;
}

} // namespace gld::ecs::aoe2
