#pragma once

// Pluggable, descriptor-driven asset loaders. A loader for descriptor D produces
// D::Asset in two phases: load_cpu (worker thread, I/O via IFileSystem + decode)
// and finalize (main thread, GL). Loaders are registered by descriptor type.

#include <memory>
#include <span>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "Desc.hpp"
#include "FileSystem.hpp"

namespace gld::ecs {

    namespace detail {
        // Extract semantic source channels from an RGBA8 decode. The mapped
        // result is what remains resident while waiting for main-thread upload.
        inline std::vector<unsigned char> pack_texture_channels(
            std::span<const unsigned char> rgba, std::size_t pixel_count,
            TextureChannelMapping mapping) {
            if (mapping == TextureChannelMapping::Default ||
                rgba.size() < pixel_count * 4u) return {};
            const std::size_t output_channels =
                mapping == TextureChannelMapping::Red ? 1u : 2u;
            std::vector<unsigned char> packed(pixel_count * output_channels);
            for (std::size_t i = 0; i < pixel_count; ++i) {
                packed[i * output_channels] = rgba[i * 4u];
                if (mapping == TextureChannelMapping::RedAlpha)
                    packed[i * 2u + 1u] = rgba[i * 4u + 3u];
            }
            return packed;
        }
    }

    template<class D>
    struct IAssetLoader {
        using Asset = typename D::Asset;
        virtual std::shared_ptr<void> load_cpu(const D& desc, const IFileSystem& fs) = 0;   // worker
        virtual std::shared_ptr<Asset> finalize(std::shared_ptr<void> cpu, const D& desc) = 0; // main/GL
        virtual ~IAssetLoader() = default;
    };

    struct LoaderRegistry {
        std::unordered_map<std::type_index, std::shared_ptr<void>> loaders; // typeid(D) -> IAssetLoader<D>

        template<class D>
        void register_loader(std::shared_ptr<IAssetLoader<D>> loader) {
            loaders[std::type_index(typeid(D))] = std::move(loader);
        }
        template<class D>
        IAssetLoader<D>* get() {
            auto it = loaders.find(std::type_index(typeid(D)));
            return it == loaders.end() ? nullptr : static_cast<IAssetLoader<D>*>(it->second.get());
        }
    };

    // ---- built-in loaders (defined in src/EcsAssetLoaders.cpp) ----
    struct TextureLoader : IAssetLoader<TextureDesc> {
        std::shared_ptr<void> load_cpu(const TextureDesc& desc, const IFileSystem& fs) override;
        std::shared_ptr<Texture<TexType::D2>> finalize(std::shared_ptr<void> cpu, const TextureDesc& desc) override;
    };

    struct ProgramLoader : IAssetLoader<ProgramDesc> {
        std::shared_ptr<void> load_cpu(const ProgramDesc& desc, const IFileSystem& fs) override;
        std::shared_ptr<Program> finalize(std::shared_ptr<void> cpu, const ProgramDesc& desc) override;
    };
}
