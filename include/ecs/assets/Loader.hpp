#pragma once

// Pluggable, descriptor-driven asset loaders. A loader for descriptor D produces
// D::Asset in two phases: load_cpu (worker thread, I/O via IFileSystem + decode)
// and finalize (main thread, GL). Loaders are registered by descriptor type.

#include <memory>
#include <typeindex>
#include <unordered_map>

#include "Desc.hpp"
#include "FileSystem.hpp"

namespace gld::ecs {

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
