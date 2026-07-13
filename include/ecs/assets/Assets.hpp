#pragma once

// Assets<T> – reference-counted, policy-driven cache for one asset type.
// Entries hold the finalised asset (shared_ptr<T>) plus load state, a weak
// reference-count token, and bookkeeping for the unload policy. Main-thread only
// (no locking): workers never touch this; they only hand back CPU data.

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace gld::ecs {

    using AssetId = std::size_t;

    enum class LoadState { NotLoaded, Loading, Loaded, Failed };

    enum class UnloadPolicy { Persistent, RefCountZero, Timed, Lru };

    struct UnloadConfig {
        UnloadPolicy policy = UnloadPolicy::RefCountZero;
        double ttl_sec = 5.0;       // Timed
        std::size_t lru_budget = 128; // Lru: max unreferenced entries kept
    };

    template<class T>
    struct Assets {
        struct StoreLifetime {
            Assets* owner = nullptr;
        };

        struct Entry {
            std::shared_ptr<T> data;
            LoadState state = LoadState::NotLoaded;
            std::weak_ptr<void> token_weak;   // alive  <=>  a strong Handle exists
            double last_used = 0.0;
            double released_at = 0.0;         // when it became unreferenced (Timed)
            bool zero_refs = false;
            std::string key;
        };

        std::unordered_map<AssetId, Entry> map;
        UnloadConfig config;
        double now_ = 0.0;   // current frame time (set by gc each frame)

        Assets() : lifetime_(std::make_shared<StoreLifetime>()) {
            lifetime_->owner = this;
        }

        ~Assets() {
            if (lifetime_) lifetime_->owner = nullptr;
        }

        Assets(const Assets&) = delete;
        Assets& operator=(const Assets&) = delete;

        Assets(Assets&& other) noexcept
            : map(std::move(other.map)),
              config(other.config),
              now_(other.now_),
              lifetime_(std::move(other.lifetime_)) {
            if (lifetime_) lifetime_->owner = this;
        }

        Assets& operator=(Assets&& other) noexcept {
            if (this == &other) return *this;
            if (lifetime_) lifetime_->owner = nullptr;
            map = std::move(other.map);
            config = other.config;
            now_ = other.now_;
            lifetime_ = std::move(other.lifetime_);
            if (lifetime_) lifetime_->owner = this;
            return *this;
        }

        Entry& entry(AssetId id) { return map[id]; }
        Entry* find(AssetId id) {
            auto it = map.find(id);
            return it == map.end() ? nullptr : &it->second;
        }

        bool contains(AssetId id) const { return map.count(id) != 0; }

        LoadState state(AssetId id) {
            auto* e = find(id);
            return e ? e->state : LoadState::NotLoaded;
        }

        // Returns the asset if Loaded (and touches last_used); nullptr otherwise.
        T* data(AssetId id) {
            auto* e = find(id);
            if (!e || e->state != LoadState::Loaded) return nullptr;
            e->last_used = now_;
            return e->data.get();
        }

        // Acquire (or reuse) the ref-count token for id. Its deleter marks the
        // entry unreferenced when the last Handle to id is destroyed.
        std::shared_ptr<void> acquire_token(AssetId id) {
            auto& e = map[id];
            if (auto t = e.token_weak.lock()) return t;
            std::weak_ptr<StoreLifetime> lifetime = lifetime_;
            std::shared_ptr<void> tok(reinterpret_cast<void*>(1),
                [lifetime, id](void*) {
                    if (auto guard = lifetime.lock(); guard && guard->owner)
                        guard->owner->on_zero_refs(id);
                });
            e.token_weak = tok;
            e.zero_refs = false;
            e.released_at = 0.0;
            return tok;
        }

        void set_loading(AssetId id, std::string key) {
            auto& e = map[id];
            e.state = LoadState::Loading;
            e.key = std::move(key);
        }
        void set_loaded(AssetId id, std::shared_ptr<T> data) {
            auto& e = map[id];
            e.data = std::move(data);
            e.state = e.data ? LoadState::Loaded : LoadState::Failed;
        }
        void set_failed(AssetId id) { map[id].state = LoadState::Failed; }

        void on_zero_refs(AssetId id) {
            if (auto* e = find(id)) { e->zero_refs = true; }
        }

        // Enforce the unload policy. Called each frame (main thread).
        void gc(double now) {
            now_ = now;
            switch (config.policy) {
            case UnloadPolicy::Persistent:
                break;
            case UnloadPolicy::RefCountZero:
                erase_if_zero([](const Entry&) { return true; });
                break;
            case UnloadPolicy::Timed:
                for (auto& [id, e] : map) {
                    if (e.zero_refs && e.released_at == 0.0) e.released_at = now;
                    if (!e.zero_refs) e.released_at = 0.0;
                }
                erase_if_zero([&](const Entry& e) { return now - e.released_at >= config.ttl_sec; });
                break;
            case UnloadPolicy::Lru:
                gc_lru();
                break;
            }
        }

    private:
        std::shared_ptr<StoreLifetime> lifetime_;

        template<class Pred>
        void erase_if_zero(Pred pred) {
            for (auto it = map.begin(); it != map.end();) {
                Entry& e = it->second;
                if (e.zero_refs && e.token_weak.expired() && pred(e)) it = map.erase(it);
                else ++it;
            }
        }

        void gc_lru() {
            std::vector<AssetId> unref;
            for (auto& [id, e] : map)
                if (e.zero_refs && e.token_weak.expired()) unref.push_back(id);
            if (unref.size() <= config.lru_budget) return;
            std::sort(unref.begin(), unref.end(), [&](AssetId a, AssetId b) {
                return map[a].last_used > map[b].last_used; // newest first
            });
            for (std::size_t i = config.lru_budget; i < unref.size(); ++i)
                map.erase(unref[i]);
        }
    };
}
