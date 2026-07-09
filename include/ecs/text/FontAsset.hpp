#pragma once

// Font asset for the ECS pipeline. A FontAsset owns BOTH the raw font-file
// bytes and the FreeType face built from them: FT_New_Memory_Face does not copy
// the buffer, so the bytes must outlive the face. Loading is split across
// threads by the asset system: load_cpu (worker) only reads bytes through the
// IFileSystem; finalize (main thread) builds the ft2::Face (FreeType is not
// safe to drive from several workers against one library).

#include <memory>
#include <vector>
#include <cstddef>

#include <ft2pp.hpp>

#include "../assets/Desc.hpp"
#include "../assets/Loader.hpp"
#include "../assets/FileSystem.hpp"

namespace gld::ecs {

    struct FontAsset {
        std::shared_ptr<std::vector<unsigned char>> bytes; // kept alive for the face
        std::shared_ptr<ft2::Face> face;                   // built from `bytes`
    };

    // (path, faceIndex)
    struct FontDesc : BaseAssetDesc<FontDesc, std::string, int> {
        using Asset = FontAsset;
        using BaseAssetDesc::BaseAssetDesc;
        const std::string& path() const { return get<0>(); }
        int face_index()          const { return get<1>(); }
    };

    // Owns a single process-wide ft2::Library (FreeType allows only one),
    // used only on the main thread in finalize().
    struct FontLoader : IAssetLoader<FontDesc> {
        std::shared_ptr<void> load_cpu(const FontDesc& desc, const IFileSystem& fs) override;
        std::shared_ptr<FontAsset> finalize(std::shared_ptr<void> cpu, const FontDesc& desc) override;

    private:
        ft2::Library& library();
        std::unique_ptr<ft2::Library> lib_;
    };
}
