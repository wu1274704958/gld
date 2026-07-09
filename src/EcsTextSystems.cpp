#include <ecs/text/TextSystems.hpp>
#include <ecs/text/TextComponents.hpp>
#include <ecs/text/GlyphAtlas.hpp>
#include <ecs/text/FontAsset.hpp>
#include <ecs/assets/AssetServer.hpp>

#include <algorithm>
#include <vector>
#include <memory>

#include <glm/gtc/matrix_transform.hpp>

namespace gld::ecs {

    namespace {

        // Build a TextLayout for `t` using `atlas` (which rasterises + uploads on
        // demand). Layout is in the entity's local pixel space, y-up, with the
        // block anchored per t.anchor about the entity origin.
        void build_layout(GlyphAtlasAA& atlas, const Text& t,
                           const std::shared_ptr<ft2::Face>& face, TextLayout& out) {
            const float lineH  = static_cast<float>(t.size) + t.leading;
            const float spaceW = 0.5f * static_cast<float>(t.size);
            const float tabW   = spaceW * 4.f;

            struct G { GlyphAtlasAA::GlyphInfo gi; float pen_x; };
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

            // Map the block anchor point to the entity origin (top of block at y=0).
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
                    q.uv    = g.gi.uv;
                    q.color = t.color;
                    q.local = glm::translate(glm::mat4(1.f), glm::vec3(cx, cy, 0.f))
                            * glm::scale(glm::mat4(1.f), glm::vec3(w, h, 1.f));
                    out.quads.push_back(std::move(q));
                }
                cell_top -= lineH;
            }
        }

    } // namespace

    void text_layout_system(EcsWorld& w) {
        auto& atlas = w.resource_or_add<GlyphAtlasAA>();
        auto& reg = w.reg();

        auto view = reg.view<Text>();
        for (auto e : view) {
            auto& t = view.get<Text>(e);

            FontAsset* fa = t.font.get();
            if (!fa || !fa->face) continue;   // font not loaded yet

            if (auto* layout = reg.try_get<TextLayout>(e); layout && layout->src_rev == t.rev)
                continue;                      // unchanged

            TextLayout out;
            out.src_rev = t.rev;
            build_layout(atlas, t, fa->face, out);

            if (auto* layout = reg.try_get<TextLayout>(e)) *layout = std::move(out);
            else reg.emplace<TextLayout>(e, std::move(out));
        }
    }

    void TextPlugin(App& app) {
        // Requires AssetPlugin (AssetServer) to have been added first.
        auto& srv = app.world.resource<AssetServer>();
        srv.register_loader<FontDesc>(std::make_shared<FontLoader>());

        app.world.resource_or_add<GlyphAtlasAA>();
        app.add_system(Stage::Update, text_layout_system);
    }
}
