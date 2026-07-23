#pragma once

// Asset descriptors. A descriptor carries ALL load parameters for an asset and
// is the single source of the cache key. Instead of hand-writing a key per type
// (error-prone, collision-prone), descriptors derive from a CRTP variadic base
// that generates the key with ONE uniform rule, composited with the concrete
// descriptor type -> different types (or params) never collide.
//
//   struct TextureDesc : BaseAssetDesc<TextureDesc, std::string, Channels, ...>
//   { using Asset = Texture<TexType::D2>; ... };
//
//   desc.key()  ->  "<TypeTag>\x1f<p0>\x1f<p1>..."

#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <concepts>

#include <program.hpp>
#include <texture.hpp>

namespace gld::ecs {

    // A plain, serialisable descriptor parameter: arithmetic / bool / enum /
    // string. (std::formattable is C++23; we stay on C++20 with a manual rule.)
    template<class P>
    concept DescParam =
        std::is_arithmetic_v<std::remove_cvref_t<P>> ||
        std::is_enum_v<std::remove_cvref_t<P>> ||
        std::same_as<std::remove_cvref_t<P>, std::string> ||
        std::same_as<std::remove_cvref_t<P>, std::string_view>;

    // One uniform serialisation rule for every parameter kind.
    template<DescParam P>
    inline std::string to_key(const P& p) {
        using U = std::remove_cvref_t<P>;
        if constexpr (std::is_enum_v<U>)
            return std::to_string(static_cast<std::underlying_type_t<U>>(p));
        else if constexpr (std::is_same_v<U, bool>)
            return p ? "1" : "0";
        else if constexpr (std::is_same_v<U, std::string>)
            return p;
        else if constexpr (std::is_same_v<U, std::string_view>)
            return std::string(p);
        else
            return std::to_string(p); // remaining arithmetic
    }

    template<class Derived, DescParam... Ts>
    struct BaseAssetDesc {
        std::tuple<Ts...> params;

        BaseAssetDesc() = default;
        explicit BaseAssetDesc(Ts... vs) : params(std::move(vs)...) {}

        template<std::size_t I> auto&       get()       { return std::get<I>(params); }
        template<std::size_t I> const auto& get() const { return std::get<I>(params); }
        template<std::size_t I, class V>
        Derived& set(V&& v) {
            std::get<I>(params) = std::forward<V>(v);
            return static_cast<Derived&>(*this);
        }

        // Collision-safe: type tag (composits the concrete descriptor type) +
        // uniformly serialised params joined by a separator that cannot appear
        // in the inputs.
        std::string key() const {
            std::string k = type_tag();
            std::apply([&](const auto&... p) {
                ((k += SEP, k += to_key(p)), ...);
            }, params);
            return k;
        }

        static std::string type_tag() { return typeid(Derived).name(); }
        static constexpr char SEP = '\x1f';
    };

    // ---- concrete descriptors (tiny) ----
    enum class Channels { Auto = 0, Gray = 1, RG = 2, RGB = 3, RGBA = 4 };
    enum class TextureChannelMapping { Default, Red, RedAlpha };
    enum class TextureFilter { Nearest, Linear };
    enum class TextureWrap { Repeat, ClampToEdge };

    inline constexpr unsigned int texture_channel_count(Channels channels,
                                                        unsigned int auto_fallback = 4u) {
        return channels == Channels::Auto ? auto_fallback
            : static_cast<unsigned int>(channels);
    }

    inline constexpr bool valid_channel_mapping(Channels channels,
                                                TextureChannelMapping mapping) {
        return mapping == TextureChannelMapping::Default ||
            (mapping == TextureChannelMapping::Red && channels == Channels::Gray) ||
            (mapping == TextureChannelMapping::RedAlpha && channels == Channels::RG);
    }

    struct ProgramDesc
        : BaseAssetDesc<ProgramDesc, std::string, std::string, std::string, std::string, std::string> {
        using Asset = Program;                       // (vs, fs, gs="", tcs="", tes="")
        using BaseAssetDesc::BaseAssetDesc;
        const std::string& vs()  const { return get<0>(); }
        const std::string& fs()  const { return get<1>(); }
        const std::string& gs()  const { return get<2>(); }
        const std::string& tcs() const { return get<3>(); }
        const std::string& tes() const { return get<4>(); }
    };

    struct TextureDesc
        : BaseAssetDesc<TextureDesc, std::string, Channels, bool, bool, bool,
                        TextureFilter, TextureFilter, TextureWrap, TextureWrap,
                        TextureChannelMapping> {
        using Asset = Texture<TexType::D2>;
        using Base = BaseAssetDesc<TextureDesc, std::string, Channels, bool, bool, bool,
                                   TextureFilter, TextureFilter, TextureWrap, TextureWrap,
                                   TextureChannelMapping>;
        TextureDesc() = default;
        TextureDesc(std::string path, Channels channels, bool flip, bool srgb, bool mipmap,
                    TextureChannelMapping mapping = TextureChannelMapping::Default)
            : Base(std::move(path), channels, flip, srgb, mipmap,
                   TextureFilter::Linear, TextureFilter::Linear,
                   TextureWrap::Repeat, TextureWrap::Repeat, mapping) {}
        TextureDesc(std::string path, Channels channels, bool flip, bool srgb, bool mipmap,
                    TextureFilter min_filter, TextureFilter mag_filter,
                    TextureWrap wrap_s, TextureWrap wrap_t,
                    TextureChannelMapping mapping = TextureChannelMapping::Default)
            : Base(std::move(path), channels, flip, srgb, mipmap,
                   min_filter, mag_filter, wrap_s, wrap_t, mapping) {}
        const std::string& path() const { return get<0>(); }
        Channels channels()       const { return get<1>(); }
        bool flip()   const { return get<2>(); }
        bool srgb()   const { return get<3>(); }
        bool mipmap() const { return get<4>(); }
        TextureFilter min_filter() const { return get<5>(); }
        TextureFilter mag_filter() const { return get<6>(); }
        TextureWrap wrap_s() const { return get<7>(); }
        TextureWrap wrap_t() const { return get<8>(); }
        TextureChannelMapping channel_mapping() const { return get<9>(); }
    };
}
