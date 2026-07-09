#include <ecs/text/FontAsset.hpp>
#include <cstring>

namespace gld::ecs {

    ft2::Library& FontLoader::library() {
        if (!lib_) lib_ = std::make_unique<ft2::Library>();
        return *lib_;
    }

    // Worker thread: just pull the raw bytes through the file system.
    std::shared_ptr<void> FontLoader::load_cpu(const FontDesc& desc, const IFileSystem& fs) {
        auto raw = fs.read_bytes(desc.path());
        if (!raw) return nullptr;
        auto bytes = std::make_shared<std::vector<unsigned char>>(raw->size());
        std::memcpy(bytes->data(), raw->data(), raw->size());
        return bytes;
    }

    // Main thread: build the FreeType face from the (retained) bytes.
    std::shared_ptr<FontAsset> FontLoader::finalize(std::shared_ptr<void> cpu, const FontDesc& desc) {
        auto bytes = std::static_pointer_cast<std::vector<unsigned char>>(cpu);
        if (!bytes || bytes->empty()) return nullptr;

        ft2::Face face = library().load_face_for_mem<ft2::Face>(
            bytes->data(), bytes->size(), desc.face_index());

        auto asset = std::make_shared<FontAsset>();
        asset->bytes = bytes; // keep the buffer alive for as long as the face lives
        asset->face  = std::make_shared<ft2::Face>(std::move(face));
        return asset;
    }
}
