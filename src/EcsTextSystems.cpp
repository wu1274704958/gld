#include <ecs/text/TextSystems.hpp>
#include <ecs/text/TextComponents.hpp>
#include <ecs/text/GlyphAtlas.hpp>
#include <ecs/text/FontAsset.hpp>
#include <ecs/assets/AssetServer.hpp>

#include <algorithm>
#include <cmath>
#include <vector>
#include <memory>

#include <glm/gtc/matrix_transform.hpp>

namespace gld::ecs {

    namespace {

        // Build a TextLayout for `t` using `atlas`. Layout is in the entity's
        // local pixel space, y-up, block anchored per t.anchor about the origin.
        // `margin` (px) expands each glyph quad + its uv `pad` so an offset drop
        // shadow has room to draw (0 when no shadow).
        template<class Atlas>
        void build_layout(Atlas& atlas, const Text& t,
                           const std::shared_ptr<ft2::Face>& face, float margin, TextLayout& out) {
            const float lineH  = static_cast<float>(t.size) + t.leading;
            const float spaceW = 0.5f * static_cast<float>(t.size);
            const float tabW   = spaceW * 4.f;
            const float T = static_cast<float>(Atlas::PageTy::MAXSurfaceSize);
            const float muv = margin / T;

            using GI = decltype(atlas.ensure_glyph(face, 0, 0));
            struct G { GI gi; float pen_x; };
            std::vector<std::vector<G>> rows;
            std::vector<float> row_w;
            rows.emplace_back();
            row_w.push_back(0.f);
            float pen = 0.f;

            auto newline = [&]() {
                row_w.back() = pen;
                rows.emplace_back();
                row_w.push_back(0.f);
                pen = 0.f;
            };

            for (char32_t c : t.text) {
                if (c == U'\n') { newline(); continue; }
                if (c == U' ')  { pen += spaceW; continue; }
                if (c == U'\t') { pen += tabW;   continue; }

                auto gi = atlas.ensure_glyph(face, t.size, static_cast<uint32_t>(c));
                if (!gi.ok) continue;

                const float adv = static_cast<float>(gi.wd.advance);
                if (t.max_width > 0.f && pen > 0.f && pen + adv > t.max_width) newline();
                rows.back().push_back({ gi, pen });
                pen += adv;
            }
            row_w.back() = pen;

            float max_w = 0.f;
            for (float rw : row_w) max_w = std::max(max_w, rw);
            const float total_h = static_cast<float>(rows.size()) * lineH;
            out.size = glm::vec2(max_w, total_h);

            const float offX = -t.anchor.x * max_w;
            const float offY =  t.anchor.y * total_h;

            float cell_top = 0.f;
            for (size_t r = 0; r < rows.size(); ++r) {
                float align_shift = 0.f;
                if (t.align == TextAlign::Center)     align_shift = (max_w - row_w[r]) * 0.5f;
                else if (t.align == TextAlign::Right) align_shift = (max_w - row_w[r]);

                for (auto& g : rows[r]) {
                    const auto& wd = g.gi.wd;
                    const float w = static_cast<float>(wd.w);
                    const float h = static_cast<float>(wd.h);
                    const float cx = offX + align_shift + g.pen_x + static_cast<float>(wd.off_x) + w * 0.5f;
                    const float cy = offY + cell_top - static_cast<float>(wd.off_y) - h * 0.5f;

                    GlyphQuad q;
                    q.atlas = g.gi.atlas;
                    q.rect  = g.gi.uv;
                    q.pad   = glm::vec4(g.gi.uv.x - muv, g.gi.uv.y - muv,
                                        g.gi.uv.z + 2.f * muv, g.gi.uv.w + 2.f * muv);
                    q.color = t.color;
                    // Quad enlarged by margin on all sides (centred on the glyph).
                    q.local = glm::translate(glm::mat4(1.f), glm::vec3(cx, cy, 0.f))
                            * glm::scale(glm::mat4(1.f), glm::vec3(w + 2.f * margin, h + 2.f * margin, 1.f));
                    out.quads.push_back(std::move(q));
                }
                cell_top -= lineH;
            }
        }

        TextRenderMode resolve_backend(const TextEffects* fx) {
            if (!fx) return TextRenderMode::AA;
            if (fx->mode != TextRenderMode::Auto) return fx->mode;
            return fx->outline ? TextRenderMode::SDF : TextRenderMode::AA;
        }

        float shadow_margin(const TextEffects* fx) {
            if (!fx || !fx->shadow) return 0.f;
            float o = std::max(std::abs(fx->shadow_offset.x), std::abs(fx->shadow_offset.y));
            return std::ceil(o + fx->shadow_softness + 2.f);
        }

        // Layout revision: re-lay-out when the text OR its effects change.
        uint64_t layout_rev(const Text& t, const TextEffects* fx) {
            uint64_t r = t.rev * 1099511628211ull;
            if (fx) r ^= (static_cast<uint64_t>(fx->rev) * 2654435761ull
                          + static_cast<uint64_t>(fx->mode) * 40503ull + 1ull);
            return r;
        }

    } // namespace

    void text_layout_system(EcsWorld& w) {
        auto& aa  = w.resource_or_add<GlyphAtlasAA>();
        auto& sdf = w.resource_or_add<GlyphAtlasSDF>();
        auto& reg = w.reg();

        auto view = reg.view<Text>();
        for (auto e : view) {
            auto& t = view.get<Text>(e);

            FontAsset* fa = t.font.get();
            if (!fa || !fa->face) continue;   // font not loaded yet

            auto* fx = reg.try_get<TextEffects>(e);
            const uint64_t want = layout_rev(t, fx);

            if (auto* layout = reg.try_get<TextLayout>(e); layout && layout->src_rev == want)
                continue;                      // unchanged

            const TextRenderMode backend = resolve_backend(fx);
            const float margin = shadow_margin(fx);

            TextLayout out;
            out.src_rev = want;
            out.backend = backend;
            if (backend == TextRenderMode::SDF)
                build_layout(sdf, t, fa->face, margin, out);
            else
                build_layout(aa, t, fa->face, margin, out);

            if (auto* layout = reg.try_get<TextLayout>(e)) *layout = std::move(out);
            else reg.emplace<TextLayout>(e, std::move(out));
        }
    }

    void TextPlugin(App& app) {
        // Requires AssetPlugin (AssetServer) to have been added first.
        auto& srv = app.world.resource<AssetServer>();
        srv.register_loader<FontDesc>(std::make_shared<FontLoader>());

        app.world.resource_or_add<GlyphAtlasAA>();
        app.world.resource_or_add<GlyphAtlasSDF>();
        app.add_system(Stage::Update, text_layout_system);
    }
}
