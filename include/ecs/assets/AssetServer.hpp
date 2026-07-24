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
#include <atomic>
#include <chrono>
#include <type_traits>

#include "../EcsWorld.hpp"
#include "../App.hpp"
#include "../Components.hpp"          // Time resource
#include "../PerformanceMonitoring.hpp"
#include "Desc.hpp"
#include "Handle.hpp"
#include "Assets.hpp"
#include "Loader.hpp"
#include "IoPool.hpp"
#include "FileSystem.hpp"

namespace gld::ecs {

    struct AssetServerDiagnostics {
        std::uint32_t io_queued = 0;
        std::uint32_t io_active = 0;
        std::uint32_t finalize_queued = 0;
        std::uint32_t cpu_jobs_completed = 0;
        double cpu_load_ms = 0.0;
        std::uint32_t finalized_this_frame = 0;
        double finalize_ms_this_frame = 0.0;
        std::uint32_t texture_uploads_this_frame = 0;
        std::uint64_t texture_upload_bytes_this_frame = 0;
        double texture_upload_ms_this_frame = 0.0;
    };

    struct AssetServerCounters {
        std::atomic<std::uint64_t> cpu_jobs{0};
        std::atomic<std::uint64_t> cpu_ns{0};
        std::atomic<std::uint64_t> finalized{0};
        std::atomic<std::uint64_t> finalize_ns{0};
        std::atomic<std::uint64_t> texture_uploads{0};
        std::atomic<std::uint64_t> texture_upload_bytes{0};
        std::atomic<std::uint64_t> texture_upload_ns{0};
    };

    struct AssetServer {
        EcsWorld* world = nullptr;
        std::shared_ptr<IFileSystem> fs;
        LoaderRegistry registry;
        IoPool pool{ 2 };
        CompletionQueue completion;
        AssetServerCounters counters;

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
            AssetServerCounters* metrics = &counters;
            AssetStore<T>* store = &assets;
            pool.enqueue([loader, d, fsptr, comp, metrics, store, id] {
                GLD_PERF_TIME_POINT(cpu_start);
                std::shared_ptr<void> cpu = loader->load_cpu(d, *fsptr);   // worker
                GLD_PERF_MONITOR(
                    const auto cpu_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - cpu_start).count();
                    metrics->cpu_jobs.fetch_add(1, std::memory_order_relaxed);
                    metrics->cpu_ns.fetch_add(
                        static_cast<std::uint64_t>(cpu_ns), std::memory_order_relaxed);
                );
                comp->push([loader, d, cpu, metrics, store, id] {
                    GLD_PERF_TIME_POINT(finalize_start);
                    bool loaded = false;
                    if (!cpu) {
                        store->set_failed(id);
                    } else {
                        auto asset = loader->finalize(cpu, d);             // main/GL
                        loaded = static_cast<bool>(asset);
                        if (asset) store->set_loaded(id, std::move(asset));
                        else store->set_failed(id);
                    }
                    GLD_PERF_MONITOR(
                        const auto elapsed = static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - finalize_start).count());
                        metrics->finalized.fetch_add(1, std::memory_order_relaxed);
                        metrics->finalize_ns.fetch_add(elapsed, std::memory_order_relaxed);
                        if constexpr (std::is_same_v<T, Texture<TexType::D2>>) {
                            if (loaded) {
                                metrics->texture_uploads.fetch_add(
                                    1, std::memory_order_relaxed);
                                metrics->texture_upload_ns.fetch_add(
                                    elapsed, std::memory_order_relaxed);
                            }
                            if (loaded) {
                                auto* texture = store->find(id);
                                if (!texture || !texture->data) return;
                                const auto channels = texture_channel_count(d.channels());
                                const auto bytes = static_cast<std::uint64_t>(
                                    texture->data->measure.width)
                                    * static_cast<std::uint64_t>(
                                        texture->data->measure.height) * channels;
                                metrics->texture_upload_bytes.fetch_add(
                                    bytes, std::memory_order_relaxed);
                            }
                        }
                    );
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
            if (!cpu) { assets.set_failed(id); return handle; }
            auto asset = loader->finalize(cpu, desc);
            if (asset) assets.set_loaded(id, std::move(asset));
            else assets.set_failed(id);
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
        Handle<Texture<TexType::D2>> load_texture(const std::string& path, Channels ch,
            bool flip, bool srgb, bool mipmap, TextureFilter min_filter,
            TextureFilter mag_filter, TextureWrap wrap_s, TextureWrap wrap_t,
            TextureChannelMapping mapping = TextureChannelMapping::Default) {
            return load(TextureDesc(path, ch, flip, srgb, mipmap,
                                    min_filter, mag_filter, wrap_s, wrap_t, mapping));
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
        auto& diag = w.resource_or_add<AssetServerDiagnostics>();
        for (auto& fn : srv.completion.drain()) fn();
        GLD_PERF_MONITOR(
        // Workers may enqueue more work while the drained set is finalized.
        // Report the queue that remains, rather than the number already drained.
        diag.finalize_queued = static_cast<std::uint32_t>(srv.completion.size());
        diag.io_queued = static_cast<std::uint32_t>(srv.pool.queued());
        diag.io_active = static_cast<std::uint32_t>(srv.pool.active());
        diag.cpu_jobs_completed = static_cast<std::uint32_t>(
            srv.counters.cpu_jobs.exchange(0, std::memory_order_relaxed));
        diag.cpu_load_ms = static_cast<double>(
            srv.counters.cpu_ns.exchange(0, std::memory_order_relaxed)) / 1'000'000.0;
        diag.finalized_this_frame = static_cast<std::uint32_t>(
            srv.counters.finalized.exchange(0, std::memory_order_relaxed));
        diag.finalize_ms_this_frame = static_cast<double>(
            srv.counters.finalize_ns.exchange(0, std::memory_order_relaxed)) / 1'000'000.0;
        diag.texture_uploads_this_frame = static_cast<std::uint32_t>(
            srv.counters.texture_uploads.exchange(0, std::memory_order_relaxed));
        diag.texture_upload_bytes_this_frame =
            srv.counters.texture_upload_bytes.exchange(0, std::memory_order_relaxed);
        diag.texture_upload_ms_this_frame = static_cast<double>(
            srv.counters.texture_upload_ns.exchange(0, std::memory_order_relaxed)) / 1'000'000.0;
        );
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
        app.world.resource_or_add<AssetServerDiagnostics>();
        app.add_system(Stage::PreUpdate, asset_update_system);
        app.add_system(Stage::Last, asset_gc_system);
        app.add_system(Stage::Shutdown, [](EcsWorld& w) {
            if (auto* srv = w.try_resource<AssetServer>())
                srv->shutdown();
        });
    }
}
