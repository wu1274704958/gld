#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <aoe/AoeUnitTarget.hpp>

namespace gld::ecs::aoe {

template<std::size_t N>
struct AoeFixedString {
    char value[N]{};

    consteval AoeFixedString(const char (&source)[N]) {
        std::copy_n(source, N, value);
    }

    constexpr std::string_view view() const { return {value, N - 1}; }
    constexpr bool operator==(const AoeFixedString&) const = default;
};

template<AoeFixedString Tag, int Priority>
struct AoeFormationTagPriority {
    static constexpr auto tag = Tag;
    static constexpr int priority = Priority;
};

enum class AoeFormationType { Skirmish };

struct AoeFormationMemberInfo {
    AoeUnitTarget unit{};
    std::uint32_t ordinal = 0;
    std::vector<std::string> tags;
    glm::vec2 collision_radius{0.f};
};

struct AoeFormationContext {
    std::vector<AoeFormationMemberInfo> members;
    float spacing = .75f;
};

struct AoeFormationSlot {
    AoeUnitTarget unit{};
    glm::vec2 local_offset{0.f};
    std::int64_t priority = 0;
    // Stable front-to-rear column-chain metadata. A layout generator owns
    // this topology because only it knows how its slots form columns.
    std::uint32_t chain_index = 0;
    std::uint32_t chain_order = 0;
};

// Local-space axis-aligned footprint of a generated formation. Bounds include
// each member's collision radius rather than only its slot center.
struct AoeFormationLayoutBounds {
    glm::vec2 local_min{0.f};
    glm::vec2 local_max{0.f};

    float width() const { return local_max.x - local_min.x; }
    float height() const { return local_max.y - local_min.y; }
};

struct AoeFormationLayout {
    std::vector<AoeFormationSlot> slots;
    AoeFormationLayoutBounds bounds{};
};

// Authoritative natural layout owned by the Squad. Width-constrained route
// variants are caller-owned values and must never overwrite this component.
struct AoeSquadLayoutState {
    AoeFormationLayout layout;
    std::uint64_t revision = 0;
    bool valid = false;
};

template<class T>
concept AoeFormationLayoutGenerator = requires(
    const AoeFormationContext& context, float maximum_width) {
    { T::generate(context) } ->
        std::same_as<std::optional<AoeFormationLayout>>;
    { T::generate_for_width(context, maximum_width) } ->
        std::same_as<std::optional<AoeFormationLayout>>;
};

// Compatibility name for code that referred to the old formation logic
// concept. Implementations now provide both natural and width-constrained
// generation.
template<class T>
concept AoeFormationLogic = AoeFormationLayoutGenerator<T>;

template<class... TagPriorities>
struct AoeSquareFormation {
private:
    struct Ranked {
        const AoeFormationMemberInfo* member = nullptr;
        std::int64_t priority = 0;
    };

    static consteval bool unique_tags() {
        constexpr std::array tags{TagPriorities::tag.view()...};
        for (std::size_t i = 0; i < tags.size(); ++i)
            for (std::size_t j = i + 1; j < tags.size(); ++j)
                if (tags[i] == tags[j]) return false;
        return true;
    }

    template<class Rule>
    static std::int64_t contribution(const AoeFormationMemberInfo& member) {
        return std::find(member.tags.begin(), member.tags.end(),
                         Rule::tag.view()) != member.tags.end()
            ? static_cast<std::int64_t>(Rule::priority) : 0;
    }

    static std::optional<std::vector<Ranked>> rank(
        const AoeFormationContext& context, float& maximum_radius) {
        if (!std::isfinite(context.spacing) || context.spacing < 0.f)
            return std::nullopt;
        std::vector<Ranked> ranked;
        ranked.reserve(context.members.size());
        maximum_radius = 0.f;
        for (const auto& member : context.members) {
            if (!std::isfinite(member.collision_radius.x) ||
                !std::isfinite(member.collision_radius.y) ||
                member.collision_radius.x < 0.f ||
                member.collision_radius.y < 0.f)
                return std::nullopt;
            ranked.push_back({&member, priority(member)});
            maximum_radius = std::max(maximum_radius, std::max(
                member.collision_radius.x, member.collision_radius.y));
        }
        std::stable_sort(ranked.begin(), ranked.end(),
            [](const Ranked& a, const Ranked& b) {
                if (a.priority != b.priority) return a.priority > b.priority;
                return a.member->ordinal < b.member->ordinal;
            });
        return ranked;
    }

    static AoeFormationLayout generate_columns(
        const std::vector<Ranked>& ranked, float spacing,
        float maximum_radius, std::size_t columns) {
        AoeFormationLayout result;
        const std::size_t count = ranked.size();
        if (!count) return result;
        columns = std::clamp<std::size_t>(columns, 1, count);
        const std::size_t rows = (count + columns - 1) / columns;
        const float cell = maximum_radius * 2.f + spacing;
        result.slots.reserve(count);
        bool have_bounds = false;
        std::size_t index = 0;
        for (std::size_t row = 0; row < rows; ++row) {
            const std::size_t row_count = std::min(columns, count - index);
            const float y = (static_cast<float>(rows - 1) * .5f -
                             static_cast<float>(row)) * cell;
            for (std::size_t column = 0; column < row_count; ++column) {
                const float x = (static_cast<float>(column) -
                    static_cast<float>(row_count - 1) * .5f) * cell;
                const auto& ranked_member = ranked[index];
                const glm::vec2 offset{x, y};
                result.slots.push_back({ranked_member.member->unit, offset,
                    ranked_member.priority,
                    static_cast<std::uint32_t>(column),
                    static_cast<std::uint32_t>(row)});
                const glm::vec2 low =
                    offset - ranked_member.member->collision_radius;
                const glm::vec2 high =
                    offset + ranked_member.member->collision_radius;
                if (!have_bounds) {
                    result.bounds = {low, high};
                    have_bounds = true;
                } else {
                    result.bounds.local_min =
                        glm::min(result.bounds.local_min, low);
                    result.bounds.local_max =
                        glm::max(result.bounds.local_max, high);
                }
                ++index;
            }
        }
        return result;
    }

public:
    static_assert(unique_tags(), "formation tag priority rules must be unique");

    static std::int64_t priority(const AoeFormationMemberInfo& member) {
        return (std::int64_t{0} + ... + contribution<TagPriorities>(member));
    }

    static std::optional<AoeFormationLayout> generate(
        const AoeFormationContext& context) {
        float maximum_radius = 0.f;
        auto ranked = rank(context, maximum_radius);
        if (!ranked) return std::nullopt;
        const std::size_t count = ranked->size();
        const std::size_t columns = count
            ? static_cast<std::size_t>(
                  std::ceil(std::sqrt(static_cast<double>(count))))
            : 1;
        return generate_columns(
            *ranked, context.spacing, maximum_radius, columns);
    }

    static std::optional<AoeFormationLayout> generate_for_width(
        const AoeFormationContext& context, float maximum_width) {
        if (!std::isfinite(maximum_width) || maximum_width <= 0.f)
            return std::nullopt;
        float maximum_radius = 0.f;
        auto ranked = rank(context, maximum_radius);
        if (!ranked) return std::nullopt;
        if (ranked->empty()) return AoeFormationLayout{};
        const std::size_t natural_columns = static_cast<std::size_t>(
            std::ceil(std::sqrt(static_cast<double>(ranked->size()))));
        constexpr float WidthEpsilon = 1e-5f;
        // The cell is at least twice every member's X radius, so footprint
        // width is monotonic with column count. Find the widest legal variant
        // without materializing every narrower layout.
        std::optional<AoeFormationLayout> best;
        std::size_t low = 1;
        std::size_t high = natural_columns;
        while (low <= high) {
            const std::size_t columns = low + (high - low) / 2;
            auto candidate = generate_columns(
                *ranked, context.spacing, maximum_radius, columns);
            if (candidate.bounds.width() <= maximum_width + WidthEpsilon) {
                best = std::move(candidate);
                low = columns + 1;
            } else {
                high = columns - 1;
            }
        }
        return best;
    }
};

using DefaultSkirmishFormation = AoeSquareFormation<
    AoeFormationTagPriority<"spearman", 300>,
    AoeFormationTagPriority<"cavalry", 200>,
    AoeFormationTagPriority<"scout", 100>,
    AoeFormationTagPriority<"archer", -100>>;

class AoeFormationRegistry {
public:
    using GenerateFn = std::optional<AoeFormationLayout> (*)(
        const AoeFormationContext&);
    using GenerateForWidthFn = std::optional<AoeFormationLayout> (*)(
        const AoeFormationContext&, float);

    template<AoeFormationType Type, AoeFormationLayoutGenerator T>
    void bind() {
        bind_erased(Type, &T::generate, &T::generate_for_width);
    }

    bool contains(AoeFormationType) const;
    std::optional<AoeFormationLayout> generate(
        AoeFormationType, const AoeFormationContext&) const;
    std::optional<AoeFormationLayout> generate_for_width(
        AoeFormationType, const AoeFormationContext&, float maximum_width) const;

private:
    struct Entry {
        GenerateFn generate = nullptr;
        GenerateForWidthFn generate_for_width = nullptr;
    };

    void bind_erased(AoeFormationType, GenerateFn, GenerateForWidthFn);
    std::unordered_map<AoeFormationType, Entry> entries_;
};

} // namespace gld::ecs::aoe
