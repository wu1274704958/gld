#pragma once

// AssetServer – the ECS entry point for asset loading. Ties together the loader
// registry, the background IoPool, the main-thread completion queue and the
// per-type Assets<T> caches. Descriptor-driven, async, reference-counted with a
// configurable unload policy.

#include <string>
#include <functional>
#include <vector>
#include <typeindex>
#include <unordered_set>
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

        std::unordered_set<std::type_index> gc_registered;
        std::vector<std::function<void(double)>> gc_hooks;

        ~AssetServer() { shutdown(); }

        void shutdown() {
            pool.stop();
            completion.clear();
            gc_hooks.clear();
            gc_registered.clear();
        }

        template<class D>
        void register_loader(std::shared_ptr<IAssetLoader<D>> loader) {
            registry.register_loader<D>(std::move(loader));
        }

        template<class T>
        void set_policy(UnloadConfig cfg) {
            world->resource_or_add<Assets<T>>().config = cfg;
        }

        // Async load. Returns immediately; Handle resolves once finalized.
        template<class D>
        Handle<typename D::Asset> load(const D& desc) {
            using T = typename D::Asset;
            auto& assets = ensure_assets<T>();
            const std::string key = desc.key();
            const AssetId id = std::hash<std::string>{}(key);
            auto token = assets.acquire_token(id);

            if (auto* e = assets.find(id);
                e && (e->state == LoadState::Loaded || e->state == LoadState::Loading))
                return Handle<T>{ &assets, id, token };

            assets.set_loading(id, key);
            auto* loader = registry.template get<D>();
            if (!loader) { assets.set_failed(id); return Handle<T>{ &assets, id, token }; }

            D d = desc;
            auto fsptr = fs;
            CompletionQueue* comp = &completion;
            Assets<T>* store = &assets;
            pool.enqueue([loader, d, fsptr, comp, store, id] {
                std::shared_ptr<void> cpu = loader->load_cpu(d, *fsptr);   // worker
                comp->push([loader, d, cpu, store, id] {
                    store->set_loaded(id, loader->finalize(cpu, d));       // main/GL
                });
            });
            return Handle<T>{ &assets, id, token };
        }

        // Blocking load (both phases inline on the calling/main thread).
        template<class D>
        Handle<typename D::Asset> load_sync(const D& desc) {
            using T = typename D::Asset;
            auto& assets = ensure_assets<T>();
            const std::string key = desc.key();
            const AssetId id = std::hash<std::string>{}(key);
            auto token = assets.acquire_token(id);

            if (auto* e = assets.find(id); e && e->state == LoadState::Loaded)
                return Handle<T>{ &assets, id, token };

            assets.set_loading(id, key);
            auto* loader = registry.template get<D>();
            if (!loader) { assets.set_failed(id); return Handle<T>{ &assets, id, token }; }

            auto cpu = loader->load_cpu(desc, *fs);
            assets.set_loaded(id, loader->finalize(cpu, desc));
            return Handle<T>{ &assets, id, token };
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
        Assets<T>& ensure_assets() {
            auto& assets = world->resource_or_add<Assets<T>>();
            std::type_index ti(typeid(T));
            if (gc_registered.insert(ti).second) {
                Assets<T>* store = &assets;
                gc_hooks.push_back([store](double now) { store->gc(now); });
            }
            return assets;
        }
    };

    // PreUpdate (main thread): finalize completed loads (GL) into the caches.
    inline void asset_update_system(EcsWorld& w) {
        auto& srv = w.resource<AssetServer>();
        for (auto& fn : srv.completion.drain()) fn();
    }

    // Last (main thread): enforce unload policies for every Assets<T>.
    inline void asset_gc_system(EcsWorld& w) {
        auto& srv = w.resource<AssetServer>();
        double now = 0.0;
        if (auto* t = w.try_resource<Time>()) now = t->elapsed;
        for (auto& hook : srv.gc_hooks) hook(now);
    }

    inline void AssetPlugin(App& app) {
        // Default file system (relative to current dir) unless one is already set.
        if (!app.world.has_resource<std::shared_ptr<IFileSystem>>())
            FileSystemPlugin(app, std::make_shared<StdFileSystem>("."));

        auto& srv = app.world.resource_or_add<AssetServer>();
        srv.world = &app.world;
        srv.fs = app.world.resource<std::shared_ptr<IFileSystem>>();

        srv.register_loader<TextureDesc>(std::make_shared<TextureLoader>());
        srv.register_loader<ProgramDesc>(std::make_shared<ProgramLoader>());

        app.world.resource_or_add<Time>();
        app.add_system(Stage::PreUpdate, asset_update_system);
        app.add_system(Stage::Last, asset_gc_system);
    }
}
