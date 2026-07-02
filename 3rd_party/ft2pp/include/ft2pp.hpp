#pragma once

/*
 * ft2pp.hpp  –  lightweight C++17 wrapper around FreeType 2
 *
 * Exposed API (matches what gld uses):
 *
 *   ft2::Library   – initialises FT_Library, creates Face objects from memory
 *   ft2::Face      – wraps FT_Face
 *       load_glyph(uint32_t c)
 *       get_glyph_data(OffsetPolicy)
 *           → optional<tuple<off_x, off_y, advance, w, h>>
 *       render_surface(sur, OffsetPolicy, mem-fn-ptr, x, y, alpha)
 *       set_pixel_size(w, h)
 *       select_charmap(FT_Encoding)
 *
 *   ft2::CenterOffEx  – offset policy: returns (bitmap_left, bitmap_top)
 *   ft2::AlwaysZero   – offset policy: always returns (0, 0)
 */

#include <ft2build.h>
#include FT_FREETYPE_H

#include <optional>
#include <tuple>
#include <stdexcept>
#include <cstdint>
#include <utility>

namespace ft2 {

// ---------------------------------------------------------------------------
// Offset policies
// ---------------------------------------------------------------------------

// Returns the glyph's own bearing so callers can position it on the baseline.
struct CenterOffEx {
    std::pair<int, int> operator()(FT_GlyphSlot slot) const noexcept {
        return { slot->bitmap_left, slot->bitmap_top };
    }
};

// Vertically aligns the glyph on the ascender baseline
// (ascender - bitmap_top pixels from the top = correct vertical position).
struct CenterOff {
    std::pair<int, int> operator()(FT_GlyphSlot slot) const noexcept {
        int ascender = static_cast<int>(slot->face->size->metrics.ascender >> 6);
        int oy = ascender - slot->bitmap_top;
        return { slot->bitmap_left, oy > 0 ? oy : 0 };
    }
};

// Always returns (0,0) – render the raw bitmap at exactly the supplied (x,y).
struct AlwaysZero {
    std::pair<int, int> operator()(FT_GlyphSlot) const noexcept {
        return { 0, 0 };
    }
};

// ---------------------------------------------------------------------------
// Forward declaration
// ---------------------------------------------------------------------------
class Face;

// ---------------------------------------------------------------------------
// Library
// ---------------------------------------------------------------------------
class Library {
public:
    Library() {
        if (FT_Init_FreeType(&lib_))
            throw std::runtime_error("ft2pp: FT_Init_FreeType failed");
    }

    ~Library() {
        if (lib_) FT_Done_FreeType(lib_);
    }

    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    Library(Library&& o) noexcept : lib_(o.lib_) { o.lib_ = nullptr; }
    Library& operator=(Library&& o) noexcept {
        if (lib_) FT_Done_FreeType(lib_);
        lib_ = o.lib_;
        o.lib_ = nullptr;
        return *this;
    }

    FT_Library get() const noexcept { return lib_; }

    // Load a face from a memory buffer already held by the caller.
    // Returns FaceTy by value; use std::shared_ptr<FaceTy>(new FaceTy(...))
    // at the call-site if heap allocation is needed.
    template<typename FaceTy = Face>
    FaceTy load_face_for_mem(const unsigned char* data, long size, int face_index) {
        FT_Face face = nullptr;
        FT_Error err = FT_New_Memory_Face(lib_,
            reinterpret_cast<const FT_Byte*>(data), size, face_index, &face);
        if (err)
            throw std::runtime_error("ft2pp: FT_New_Memory_Face failed");
        return FaceTy(lib_, face);
    }

private:
    FT_Library lib_ = nullptr;
};

// ---------------------------------------------------------------------------
// Face
// ---------------------------------------------------------------------------
class Face {
public:
    Face(FT_Library lib, FT_Face face) noexcept
        : lib_(lib), face_(face) {}

    Face(const Face&) = delete;
    Face& operator=(const Face&) = delete;

    Face(Face&& o) noexcept : lib_(o.lib_), face_(o.face_) {
        o.face_ = nullptr;
        o.lib_  = nullptr;
    }
    Face& operator=(Face&& o) noexcept {
        if (face_) FT_Done_Face(face_);
        lib_  = o.lib_;
        face_ = o.face_;
        o.face_ = nullptr;
        o.lib_  = nullptr;
        return *this;
    }

    ~Face() {
        if (face_) FT_Done_Face(face_);
    }

    // Load (but do not render) the glyph for Unicode codepoint c.
    void load_glyph(uint32_t c) {
        FT_UInt idx = FT_Get_Char_Index(face_, static_cast<FT_ULong>(c));
        FT_Load_Glyph(face_, idx, FT_LOAD_DEFAULT);
    }

    // Render the current glyph to a greyscale bitmap and return its metrics.
    // Returns nullopt for glyphs with zero bitmap size (e.g. whitespace).
    // Return order: (off_x, off_y, advance, width, height)
    //   off_x   = bitmap_left  (horizontal bearing, pixels from pen)
    //   off_y   = bitmap_top   (vertical   bearing, pixels above baseline)
    //   advance = horizontal advance in pixels
    //   width, height = bitmap dimensions in pixels
    template<typename OffPolicy>
    std::optional<std::tuple<int, int, int, unsigned int, unsigned int>>
    get_glyph_data(OffPolicy&& policy) {
        if (!face_ || !face_->glyph) return std::nullopt;
        // Render to bitmap so we get exact pixel dimensions.
        FT_Render_Glyph(face_->glyph, FT_RENDER_MODE_NORMAL);
        FT_GlyphSlot slot = face_->glyph;
        unsigned int w = slot->bitmap.width;
        unsigned int h = slot->bitmap.rows;
        if (w == 0 || h == 0) return std::nullopt;
        auto [ox, oy] = policy(slot);
        int advance = static_cast<int>(slot->advance.x >> 6);
        return std::make_tuple(ox, oy, advance, w, h);
    }

    // Copy the current glyph bitmap into a surface, return horizontal advance (pixels).
    //
    //   sur          – target surface
    //   off_policy   – offset policy; (ox, oy) added to (x, y) per pixel
    //   set_pixel_fn – member-function pointer: void (Sur::*)(int, int, PixelTy)
    //   x, y         – top-left atlas position to write into
    //   pixel_val    – value to write:
    //                   • unsigned char → scaled by glyph bitmap coverage (grayscale atlas)
    //                   • any other type → written as-is for every non-zero bitmap pixel
    //
    // Returns the advance width in pixels (use to advance the pen position).
    template<typename Sur, typename OffPolicy, typename SetPixelFn, typename PixelTy>
    int render_surface(Sur& sur, OffPolicy&& off_policy, SetPixelFn set_pixel_fn,
                        int x, int y, PixelTy pixel_val) {
        if (!face_ || !face_->glyph) return 0;
        FT_Render_Glyph(face_->glyph, FT_RENDER_MODE_NORMAL);
        FT_GlyphSlot  slot = face_->glyph;
        FT_Bitmap&    bmp  = slot->bitmap;
        auto [ox, oy] = off_policy(slot);

        const int sur_w = sur.w();
        const int sur_h = sur.h();

        for (unsigned int row = 0; row < bmp.rows; ++row) {
            for (unsigned int col = 0; col < bmp.width; ++col) {
                unsigned char v = bmp.buffer[row * static_cast<unsigned>(bmp.pitch) + col];
                if (v == 0) continue;
                int px = x + ox + static_cast<int>(col);
                int py = y + oy + static_cast<int>(row);
                if (px < 0 || px >= sur_w || py < 0 || py >= sur_h) continue;

                if constexpr (std::is_same_v<std::decay_t<PixelTy>, unsigned char>) {
                    unsigned char out = static_cast<unsigned char>(
                        static_cast<unsigned>(v) * static_cast<unsigned>(pixel_val) / 255u);
                    (sur.*set_pixel_fn)(px, py, out);
                } else {
                    (sur.*set_pixel_fn)(px, py, pixel_val);
                }
            }
        }
        return static_cast<int>(slot->advance.x >> 6);
    }

    void set_pixel_size(int w, int h) {
        FT_Set_Pixel_Sizes(face_,
            static_cast<FT_UInt>(w),
            static_cast<FT_UInt>(h));
    }

    void select_charmap(FT_Encoding enc) {
        FT_Select_Charmap(face_, enc);
    }

    FT_Face get() const noexcept { return face_; }

private:
    FT_Library lib_  = nullptr;  // not owned
    FT_Face    face_ = nullptr;  // owned
};

} // namespace ft2
