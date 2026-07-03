#pragma once

// cover_art.h – standalone tool to extract & sample a track's embedded album art.
//
//  CoverArt cover;
//  cover.load(chan);                 // pull embedded picture from a BASS channel
//  if (cover.valid())
//      glm::vec3 c = cover.get_color(u, v);   // sample at UV in [0,1]
//  cover.unload();                   // free pixels (also done in dtor / on reload)
//
//  Sources tried (in order): FLAC picture → MP3 ID3v2 APIC → MP4 cover art.
//  Decoded to RGBA via stb_image (implementation lives in resource_mgr.cpp; we
//  only use the declarations here — do NOT define STB_IMAGE_IMPLEMENTATION).

#include <bass.h>
#include <bassflac.h>
#include <stb_image.h>
#include <glm/glm.hpp>
#include <glm/common.hpp>
#include <vector>
#include <cstring>
#include <cstdint>

namespace fv {

class CoverArt {
public:
    ~CoverArt() { unload(); }

    // Extract the embedded cover from a playing BASS channel. Returns true on
    // success (a picture was found and decoded). On failure the object becomes
    // invalid() and callers should fall back (e.g. random noise colours).
    bool load(DWORD chan)
    {
        unload();
        if (!chan) return false;

        // 1) FLAC embedded picture (bassflac)
        if (const TAG_FLAC_PICTURE* pic =
                (const TAG_FLAC_PICTURE*)BASS_ChannelGetTags(chan, BASS_TAG_FLAC_PICTURE))
        {
            if (pic->data && pic->length > 0 && decode(pic->data, (int)pic->length))
                return true;
        }

        // 2) MP3 ID3v2 APIC frame
        if (const char* id3 = BASS_ChannelGetTags(chan, BASS_TAG_ID3V2))
        {
            if (parse_id3v2_apic((const unsigned char*)id3))
                return true;
        }

        // 3) MP4 / iTunes cover art
        if (const TAG_BINARY* mp4 =
                (const TAG_BINARY*)BASS_ChannelGetTags(chan, BASS_TAG_MP4_COVERART))
        {
            if (mp4->data && mp4->length > 0 && decode(mp4->data, (int)mp4->length))
                return true;
        }

        return false;
    }

    void unload()
    {
        rgba_.clear();
        w_ = h_ = 0;
    }

    bool valid()  const { return w_ > 0 && h_ > 0 && !rgba_.empty(); }
    int  width()  const { return w_; }
    int  height() const { return h_; }

    // Bilinear sample. u,v in [0,1]; v is treated bottom-up (flipped from image
    // top-left origin) so it lines up with a world-space "up" axis.
    glm::vec3 get_color(float u, float v) const
    {
        if (!valid()) return glm::vec3(0.f);
        u = glm::clamp(u, 0.f, 1.f);
        v = glm::clamp(v, 0.f, 1.f);

        float fx = u * (float)(w_ - 1);
        float fy = (1.f - v) * (float)(h_ - 1);
        int x0 = (int)fx, y0 = (int)fy;
        int x1 = (x0 + 1 < w_) ? x0 + 1 : x0;
        int y1 = (y0 + 1 < h_) ? y0 + 1 : y0;
        float tx = fx - (float)x0, ty = fy - (float)y0;

        glm::vec3 c00 = texel(x0, y0), c10 = texel(x1, y0);
        glm::vec3 c01 = texel(x0, y1), c11 = texel(x1, y1);
        glm::vec3 a = glm::mix(c00, c10, tx);
        glm::vec3 b = glm::mix(c01, c11, tx);
        return glm::mix(a, b, ty);
    }

private:
    glm::vec3 texel(int x, int y) const
    {
        const unsigned char* q = &rgba_[((size_t)y * w_ + x) * 4];
        return glm::vec3((float)q[0], (float)q[1], (float)q[2]) / 255.f;
    }

    bool decode(const void* data, int len)
    {
        int w = 0, h = 0, comp = 0;
        unsigned char* d = stbi_load_from_memory(
            (const stbi_uc*)data, len, &w, &h, &comp, 4);
        if (!d) return false;
        rgba_.assign(d, d + (size_t)w * h * 4);
        w_ = w; h_ = h;
        stbi_image_free(d);
        return true;
    }

    static uint32_t synchsafe(const unsigned char* b)
    {
        return ((uint32_t)(b[0] & 0x7f) << 21) | ((uint32_t)(b[1] & 0x7f) << 14) |
               ((uint32_t)(b[2] & 0x7f) << 7)  |  (uint32_t)(b[3] & 0x7f);
    }

    // Parse an ID3v2.3/2.4 tag block and decode the first APIC picture.
    bool parse_id3v2_apic(const unsigned char* id3)
    {
        if (std::memcmp(id3, "ID3", 3) != 0) return false;
        int ver = id3[3];                       // major version (3 or 4)
        uint32_t tagsize = synchsafe(id3 + 6);  // size excludes 10-byte header
        size_t pos = 10;
        size_t end = 10 + (size_t)tagsize;

        while (pos + 10 <= end) {
            const unsigned char* fr = id3 + pos;
            if (fr[0] == 0) break;              // padding

            char fid[5] = { 0 };
            std::memcpy(fid, fr, 4);

            uint32_t fsize = (ver == 4)
                ? synchsafe(fr + 4)
                : ((uint32_t)fr[4] << 24) | ((uint32_t)fr[5] << 16) |
                  ((uint32_t)fr[6] << 8)  |  (uint32_t)fr[7];
            if (fsize == 0 || pos + 10 + fsize > end) break;

            if (std::memcmp(fid, "APIC", 4) == 0) {
                const unsigned char* f = fr + 10;
                uint32_t i = 0;
                unsigned char enc = f[i++];             // text encoding
                while (i < fsize && f[i] != 0) ++i;     // MIME (ascii, null-term)
                ++i;                                    // skip null
                if (i < fsize) ++i;                     // picture-type byte
                if (enc == 1 || enc == 2) {             // UTF-16 desc (double null)
                    while (i + 1 < fsize && !(f[i] == 0 && f[i + 1] == 0)) i += 2;
                    i += 2;
                } else {                                // ISO/UTF-8 desc (single null)
                    while (i < fsize && f[i] != 0) ++i;
                    ++i;
                }
                if (i < fsize && decode(f + i, (int)(fsize - i)))
                    return true;
            }
            pos += 10 + fsize;
        }
        return false;
    }

    std::vector<unsigned char> rgba_;
    int w_ = 0, h_ = 0;
};

} // namespace fv
