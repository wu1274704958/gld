#pragma once

// GlyphAtlas – the ECS glyph-cache resource. It reuses the existing, battle-
// tested txt:: machinery (Page shelf-packer + TexGenerate GL upload + the 8-bit
// AA DefTextRender rasteriser) but keys pages directly by (face, size) taken
// from a loaded FontAsset, bypassing the old DataMgr/TextMgr singletons.
//
// Switchable backend: the atlas is templated on <TextGen, TextRender> exactly
// like txt::Page, so an SDF backend later is just a new Gen/Render pair — the
// layout / batch / render stages above are untouched. GlyphAtlasAA is the
// default 8-bit anti-aliased backend used today.
//
// GL-only-on-main-thread: ensure_glyph rasterises + uploads, so it must be
// called from a main-thread system (text_layout_system).

#include <memory>
#include <vector>
#include <utility>
#include <unordered_map>

#include <glm/glm.hpp>
#include <texture.hpp>
#include <ft2pp.hpp>

#include <text/Page.hpp>
#include <text/TexGenerate.hpp>
#include <text/TextRender.hpp>

namespace gld::ecs {

    template<class TextGen = txt::TexGenerate, class TextRender = txt::DefTextRender>
    struct GlyphAtlasT {
        static constexpr int GAP = 2;
        using PageTy = txt::Page<TextGen, TextRender, GAP>;

        struct GlyphInfo {
            std::shared_ptr<Texture<TexType::D2>> atlas;
            glm::vec4 uv{ 0.f };   // normalised (x, y, w, h)
            txt::WordData wd;
            bool ok = false;
        };

        GlyphAtlasT() = default;
        GlyphAtlasT(const GlyphAtlasT&) = delete;            // pages are move-only
        GlyphAtlasT& operator=(const GlyphAtlasT&) = delete;
        GlyphAtlasT(GlyphAtlasT&&) = default;
        GlyphAtlasT& operator=(GlyphAtlasT&&) = default;

        // Ensure codepoint `c` at `size` is rasterised into this face's atlas;
        // returns the atlas texture, its normalised uv rect and glyph metrics.
        GlyphInfo ensure_glyph(const std::shared_ptr<ft2::Face>& face, int size, uint32_t c) {
            if (!face) return {};
            Key key{ face.get(), size };
            auto& pages = buckets_[key];

            for (auto& p : pages)
                if (p.has(c)) return make_info(p, c);

            face->set_pixel_size(size, size);
            if (!pages.empty()) {
                auto& last = pages.back();
                if (last.test(c) && last.put(c)) return make_info(last, c);
            }

            pages.emplace_back(face);
            face->set_pixel_size(size, size);
            auto& np = pages.back();
            if (np.put(c)) return make_info(np, c);
            return {};
        }

    private:
        using Key = std::pair<ft2::Face*, int>;
        struct KeyHash {
            std::size_t operator()(const Key& k) const {
                return std::hash<const void*>{}(k.first) * 1315423911u ^ std::hash<int>{}(k.second);
            }
        };

        GlyphInfo make_info(PageTy& p, uint32_t c) {
            GlyphInfo gi;
            gi.atlas = p.get_generate();
            gi.wd = p.get(c).value();
            const float T = static_cast<float>(PageTy::MAXSurfaceSize);
            gi.uv = glm::vec4(gi.wd.x / T, gi.wd.y / T, gi.wd.w / T, gi.wd.h / T);
            gi.ok = static_cast<bool>(gi.atlas);
            return gi;
        }

        std::unordered_map<Key, std::vector<PageTy>, KeyHash> buckets_;
    };

    using GlyphAtlasAA = GlyphAtlasT<>;
}
