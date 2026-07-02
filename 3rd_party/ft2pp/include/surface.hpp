#pragma once

/*
 * surface.hpp  –  generic 2-D pixel surface in namespace wws
 *
 * wws::surface<Content>
 *   – Content supplies storage and pixel operations.
 *   – Designed to work with txt::GrayContent (and any similar content type).
 *
 * Required Content interface:
 *   Content(int w, int h)          – allocate storage
 *   void    init()                 – zero / clear all pixels
 *   void    set_pixel(int x, int y, PIXEL_TYPE v)
 *   PIXEL_TYPE get_pixel(int x, int y) const
 *   int  w, h                      – dimensions (public members)
 */

namespace wws {

template<typename Content>
class surface {
public:
    using pixel_type = typename Content::PIXEL_TYPE;

    surface(int w, int h) : content_(w, h) {
        content_.init();
    }

    // non-copyable (Content is usually non-copyable too)
    surface(const surface&) = delete;
    surface& operator=(const surface&) = delete;

    surface(surface&&) = default;
    surface& operator=(surface&&) = default;

    int w() const noexcept { return content_.w; }
    int h() const noexcept { return content_.h; }

    // set_pixel is taken as a member-function pointer in render_surface():
    //   face.render_surface(sur, policy, &SurTy::set_pixel, x, y, alpha);
    void set_pixel(int x, int y, pixel_type v) {
        content_.set_pixel(x, y, v);
    }

    pixel_type get_pixel(int x, int y) const {
        return content_.get_pixel(x, y);
    }

    Content& get_content() noexcept { return content_; }
    const Content& get_content() const noexcept { return content_; }

private:
    Content content_;
};

} // namespace wws
