#include <cassert>
#include <array>
#include <chrono>
#include <cmath>

#include <ecs/systems/TransformSystem.hpp>
#include <ecs/render/Batch.hpp>
#include <ecs/assets/Loader.hpp>

using namespace gld::ecs;

int main() {
    Time time;
    TimeClock clock;
    TimeSettings settings;
    settings.max_delta = 0.1f;
    const TimeClock::Clock::time_point start{};

    update_time(time, clock, settings, start);
    assert(time.frame == 1);
    assert(time.dt == 0.f);
    assert(time.raw_dt == 0.f);

    update_time(time, clock, settings, start + std::chrono::milliseconds(16));
    assert(std::abs(time.raw_dt - 0.016f) < 0.0001f);
    assert(std::abs(time.dt - 0.016f) < 0.0001f);

    update_time(time, clock, settings, start + std::chrono::milliseconds(266));
    assert(std::abs(time.raw_dt - 0.250f) < 0.0001f);
    assert(std::abs(time.dt - 0.100f) < 0.0001f);

    BatchKey one;
    one.texture_count = 1;
    one.textures[0] = 11;
    one.shader = 3;
    BatchKey same = one;
    assert(one == same);
    same.texture_count = 3;
    same.textures[1] = 12;
    same.textures[2] = 13;
    assert(!(one == same));
    assert(BatchKeyHash{}(one) != BatchKeyHash{}(same));

    auto synchronized_unit_signature = [](std::uint64_t revision) {
        std::uint64_t signature = BatchSignatureSeed;
        for (std::uint32_t entity = 3; entity < 19; ++entity)
            signature = batch_signature_append_source(signature, entity, revision, 1);
        return signature;
    };

    // Regression: summing the old per-source FNV hashes made most adjacent
    // revisions of 16 consecutive, synchronized entities collide. Every
    // logical animation revision must now dirty the batch.
    auto previous = synchronized_unit_signature(1);
    for (std::uint64_t revision = 2; revision <= 500; ++revision) {
        const auto current = synchronized_unit_signature(revision);
        assert(current != previous);
        previous = current;
    }

    std::uint64_t unchanged = BatchSignatureSeed;
    std::uint64_t one_changed = BatchSignatureSeed;
    for (std::uint32_t entity = 3; entity < 19; ++entity) {
        unchanged = batch_signature_append_source(unchanged, entity, 7, 1);
        one_changed = batch_signature_append_source(
            one_changed, entity, entity == 11 ? 8 : 7, 1);
    }
    assert(unchanged != one_changed);

    std::uint64_t forward = BatchSignatureSeed;
    std::uint64_t reverse = BatchSignatureSeed;
    for (std::uint32_t entity = 3; entity < 19; ++entity)
        forward = batch_signature_append_source(forward, entity, 7, 1);
    for (std::uint32_t entity = 19; entity-- > 3;)
        reverse = batch_signature_append_source(reverse, entity, 7, 1);
    assert(forward != reverse);

    static_assert(texture_channel_count(Channels::Gray) == 1);
    static_assert(texture_channel_count(Channels::RG) == 2);
    static_assert(texture_channel_count(Channels::RGBA) == 4);
    static_assert(valid_channel_mapping(Channels::Gray, TextureChannelMapping::Red));
    static_assert(valid_channel_mapping(Channels::RG, TextureChannelMapping::RedAlpha));
    static_assert(!valid_channel_mapping(Channels::RGBA, TextureChannelMapping::Red));

    const TextureDesc gray_default("mask.png", Channels::Gray, false, false, false);
    const TextureDesc source_red("mask.png", Channels::Gray, false, false, false,
                                 TextureChannelMapping::Red);
    assert(gray_default.key() != source_red.key());

    const TextureDesc rg_default("mask.png", Channels::RG, false, false, false);
    const TextureDesc source_red_alpha("mask.png", Channels::RG, false, false, false,
                                       TextureChannelMapping::RedAlpha);
    assert(rg_default.key() != source_red_alpha.key());

    const std::array<unsigned char, 12> rgba = {
        7, 91, 37, 255, // maximum subcolor, covered
        0, 44, 22, 255, // subcolor zero must remain distinguishable from no coverage
        3, 11, 99,   0  // transparent mask pixel
    };
    const auto red = detail::pack_texture_channels(
        rgba, 3, TextureChannelMapping::Red);
    assert((red == std::vector<unsigned char>{7, 0, 3}));
    const auto red_alpha = detail::pack_texture_channels(
        rgba, 3, TextureChannelMapping::RedAlpha);
    assert((red_alpha == std::vector<unsigned char>{7, 255, 0, 255, 3, 0}));
    return 0;
}
