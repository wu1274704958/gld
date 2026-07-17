#pragma once

// AssetServer – the ECS entry point for asset loading. Ties together the loader
// registry, the background IoPool, the main-thread completion queue and the
// per-type Assets<T> caches. Descriptor-driven, async, reference-counted with a
// configurable unload policy.

#include <string>
#include <functional>
#include <vector>
#include <typeindex>
#include <memory>

#include "../EcsWorld.hpp"
#include "../App.hpp"
#include "../Components.hpp"          // Time resource
#include "Desc.hpp"
#include "Handle.hpp"
#include "Assets.hpp"
#include "Loader.hpp"
#include "IoPool.hpp"
#include "FileSystem.hpp"

namespace gld::ecs {

    struct AssetServer {
        EcsWorld* world = nullptr;
        std::shared_ptr<IFileSystem> fs;
        LoaderRegistry registry;
        IoPool pool{ 2 };
        CompletionQueue completion;

        ~AssetServer() { shutdown(); }

        void shutdown() {
            pool.stop();
            completion.clear();
        }

        template<class D>
        void register_loader(std::shared_ptr<IAssetLoader<D>> loader) {
            registry.register_loader<D>(std::move(loader));
        }

        template<class T>
        void set_policy(UnloadConfig cfg) {
            world->resource_or_add<AssetManager>().store<T>().set_policy(cfg);
        }

        // Async load. Returns immediately; Handle resolves once finalized.
        template<class D>
        Handle<typename D::Asset> load(const D& desc, AssetLoadOptions options = {}) {
            using T = typename D::Asset;
            auto& assets = ensure_store<T>();
            const std::string key = desc.key();
            const AssetId id = std::hash<std::string>{}(key);
            auto handle = assets.acquire(id, key, options.gc_policy);

            if (auto* e = assets.find(id);
                e && (e->header.state == LoadState::Loaded || e->header.state == LoadState::Loading))
                return handle;

            assets.set_loading(id, key);
            auto* loader = registry.template get<D>();
            if (!loader) { assets.set_failed(id); return handle; }

            D d = desc;
            auto fsptr = fs;
            CompletionQueue* comp = &completion;
            AssetStore<T>* store = &assets;
            pool.enqueue([loader, d, fsptr, comp, store, id] {
                std::shared_ptr<void> cpu = loader->load_cpu(d, *fsptr);   // worker
                comp->push([loader, d, cpu, store, id] {
                    store->set_loaded(id, loader->finalize(cpu, d));       // main/GL
                });
            });
            return handle;
        }

        // Blocking load (both phases inline on the calling/main thread).
        template<class D>
        Handle<typename D::Asset> load_sync(const D& desc, AssetLoadOptions options = {}) {
            using T = typename D::Asset;
            auto& assets = ensure_store<T>();
            const std::string key = desc.key();
            const AssetId id = std::hash<std::string>{}(key);
            auto handle = assets.acquire(id, key, options.gc_policy);

            if (auto* e = assets.find(id); e && e->header.state == LoadState::Loaded)
                return handle;

            assets.set_loading(id, key);
            auto* loader = registry.template get<D>();
            if (!loader) { assets.set_failed(id); return handle; }

            auto cpu = loader->load_cpu(desc, *fs);
            assets.set_loaded(id, loader->finalize(cpu, desc));
            return handle;
        }

        // ---- convenience wrappers ----
        Handle<Program> load_program(const std::string& vs, const std::string& fs_) {
            return load(ProgramDesc(vs, fs_, "", "", ""));
        }
        Handle<Program> load_program(const std::string& vs, const std::string& fs_, const std::string& gs) {
            return load(ProgramDesc(vs, fs_, gs, "", ""));
        }
        Handle<Texture<TexType::D2>> load_texture(const std::string& path,
            Channels ch = Channels::RGBA, bool flip = false, bool srgb = false) {
            return load(TextureDesc(path, ch, flip, srgb, true));
        }

    private:
        template<class T>
        AssetStore<T>& ensure_store() {
            return world->resource_or_add<AssetManager>().store<T>();
        }
    };

    // PreUpdate (main thread): finalize completed loads (GL) into the caches.
    inline void asset_update_system(EcsWorld& w) {
        auto& srv = w.resource<AssetServer>();
        for (auto& fn : srv.completion.drain()) fn();
    }

    // Last (main thread): enforce unload policies for every Assets<T>.
    inline void asset_gc_system(EcsWorld& w) {
        auto& manager = w.resource<AssetManager>();
        double now = 0.0;
        if (auto* t = w.try_resource<Time>()) now = t->elapsed;
        manager.gc(now);
    }

    inline void AssetPlugin(App& app) {
        // Default file system (relative to current dir) unless one is already set.
        if (!app.world.has_resource<std::shared_ptr<IFileSystem>>())
            FileSystemPlugin(app, std::make_shared<StdFileSystem>("."));

        app.world.resource_or_add_with_priority<AssetManager>(
            static_cast<int>(ResourceCleanupPriority::AssetManager));

        auto& srv = app.world.resource_or_add_with_priority<AssetServer>(
            static_cast<int>(ResourceCleanupPriority::AssetServer));
        srv.world = &app.world;
        srv.fs = app.world.resource<std::shared_ptr<IFileSystem>>();

        srv.register_loader<TextureDesc>(std::make_shared<TextureLoader>());
        srv.register_loader<ProgramDesc>(std::make_shared<ProgramLoader>());

        app.world.resource_or_add<Time>();
        app.add_system(Stage::PreUpdate, asset_update_system);
        app.add_system(Stage::Last, asset_gc_system);
        app.add_system(Stage::Shutdown, [](EcsWorld& w) {
            if (auto* srv = w.try_resource<AssetServer>())
                srv->shutdown();
        });
    }
}
