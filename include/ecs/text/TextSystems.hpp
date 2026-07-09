#pragma once

// Text layout stage. text_layout_system turns each Text into a TextLayout (a
// list of positioned glyph quads) using the GlyphAtlas. It runs on the main
// thread (it rasterises/uploads glyphs on demand) and is dirty-gated: a Text is
// only re-laid-out when its `rev` changes. TextPlugin wires the font loader, the
// atlas resource and the layout system into an App.

#include "../App.hpp"
#include "../EcsWorld.hpp"

namespace gld::ecs {

    // Main thread. Builds/refreshes TextLayout for every Text whose font is
    // loaded and whose rev differs from the cached layout.
    void text_layout_system(EcsWorld& w);

    void TextPlugin(App& app);
}
