#pragma once

// Asset storage primitives.
//
// The asset cache uses intrusive entry ref-counts instead of shared_ptr tokens.
// AssetHandle<T> keeps an AssetEntry alive by incrementing the entry's
// strong_refs. GC reclaims entries only after strong_refs reaches zero and the
// configured static GC policy says the entry can be reclaimed.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gld::ecs {

    using AssetId = std::size_t;
    using AssetTypeId = std::type_index;

    enum class LoadState { NotLoaded, Loading, Loaded, Failed };

    enum class GcPolicyId : std::uint8_t {
        Default,
        Persistent,
        RefCountZero,
        Timed,
        Lru
    };

    enum class UnloadPolicy { Persistent, RefCountZero, Timed, Lru };

    struct UnloadConfig {
        UnloadPolicy policy = UnloadPolicy::RefCountZero;
        double ttl_sec = 5.0;
        std::size_t lru_budget = 128;
    };

    struct AssetLoadOptions {
        GcPolicyId gc_policy = GcPolicyId::Default;
    };

    struct GcConfig {
        double ttl_sec = 5.0;
        std::size_t lru_budget = 128;
    };

    inline GcPolicyId to_gc_policy_id(UnloadPolicy policy) {
        switch (policy) {
        case UnloadPolicy::Persistent: return GcPolicyId::Persistent;
        case UnloadPolicy::Timed: return GcPolicyId::Timed;
        case UnloadPolicy::Lru: return GcPolicyId::Lru;
        case UnloadPolicy::RefCountZero:
        default: return GcPolicyId::RefCountZero;
        }
    }

    template<class... Ts>
    struct TypeList {};

    struct EntrySlot {
        std::uint32_t index = UINT32_MAX;
        std::uint32_t generation = 0;
        explicit operator bool() const { return index != UINT32_MAX; }
        bool operator==(const EntrySlot&) const = default;
    };

    struct AssetEntryHeader {
        AssetId id = 0;
        std::uint32_t generation = 1;
        std::uint32_t strong_refs = 0;
        LoadState state = LoadState::NotLoaded;
        GcPolicyId gc_policy = GcPolicyId::RefCountZero;
        double last_used = 0.0;
        double released_at = 0.0;
        bool pending_reclaim = false;
    };

    struct AssetStoreStats {
        AssetTypeId type{ typeid(void) };
        std::size_t live_entries = 0;
        std::size_t loaded_entries = 0;
        std::size_t loading_entries = 0;
        std::size_t unreferenced_entries = 0;
    };

    struct AssetStats {
        std::vector<AssetStoreStats> stores;
    };

    template<class Entry>
    class AssetEntryPool {
    public:
        template<class... Args>
        EntrySlot allocate(Args&&... args) {
            if (!free_.empty()) {
                const std::uint32_t index = free_.back();
                free_.pop_back();
                Slot& slot = slots_[index];
                slot.entry.emplace(std::forward<Args>(args)...);
                return EntrySlot{ index, slot.generation };
            }

            Slot slot;
            slot.generation = 1;
            slot.entry.emplace(std::forward<Args>(args)...);
            slots_.push_back(std::move(slot));
            return EntrySlot{ static_cast<std::uint32_t>(slots_.size() - 1), 1 };
        }

        void release(EntrySlot slot_id) {
            Slot* slot = raw_slot(slot_id.index);
            if (!slot || slot->generation != slot_id.generation || !slot->entry) return;
            slot->entry.reset();
            ++slot->generation;
            free_.push_back(slot_id.index);
        }

        Entry* get(EntrySlot slot_id) {
            Slot* slot = raw_slot(slot_id.index);
            if (!slot || slot->generation != slot_id.generation || !slot->entry) return nullptr;
            return &*slot->entry;
        }

        const Entry* get(EntrySlot slot_id) const {
            const Slot* slot = raw_slot(slot_id.index);
            if (!slot || slot->generation != slot_id.generation || !slot->entry) return nullptr;
            return &*slot->entry;
        }

        template<class Fn>
        void for_each_live(Fn&& fn) {
            for (std::uint32_t i = 0; i < slots_.size(); ++i) {
                Slot& slot = slots_[i];
                if (slot.entry) fn(EntrySlot{ i, slot.generation }, *slot.entry);
            }
        }

        template<class Fn>
        void for_each_live(Fn&& fn) const {
            for (std::uint32_t i = 0; i < slots_.size(); ++i) {
                const Slot& slot = slots_[i];
                if (slot.entry) fn(EntrySlot{ i, slot.generation }, *slot.entry);
            }
        }

    private:
        struct Slot {
            std::uint32_t generation = 1;
            std::optional<Entry> entry;
        };

        Slot* raw_slot(std::uint32_t index) {
            return index < slots_.size() ? &slots_[index] : nullptr;
        }

        const Slot* raw_slot(std::uint32_t index) const {
            return index < slots_.size() ? &slots_[index] : nullptr;
        }

        std::vector<Slot> slots_;
        std::vector<std::uint32_t> free_;
    };

    struct PersistentGc {
        static constexpr GcPolicyId id = GcPolicyId::Persistent;
        static bool should_reclaim(const AssetEntryHeader&, double, const GcConfig&) { return false; }
    };

    struct RefCountZeroGc {
        static constexpr GcPolicyId id = GcPolicyId::RefCountZero;
        static bool should_reclaim(const AssetEntryHeader& entry, double, const GcConfig&) {
            return entry.strong_refs == 0;
        }
    };

    struct TimedGc {
        static constexpr GcPolicyId id = GcPolicyId::Timed;
        static bool should_reclaim(const AssetEntryHeader& entry, double now, const GcConfig& cfg) {
            return entry.strong_refs == 0 && entry.released_at > 0.0 && now - entry.released_at >= cfg.ttl_sec;
        }
    };

    struct LruGc {
        static constexpr GcPolicyId id = GcPolicyId::Lru;
        static bool should_reclaim(const AssetEntryHeader& entry, double, const GcConfig&) {
            return entry.strong_refs == 0 && entry.pending_reclaim;
        }
    };

    template<class... Policies>
    bool should_reclaim_by_policy(TypeList<Policies...>, const AssetEntryHeader& entry, double now, const GcConfig& cfg) {
        bool handled = false;
        bool reclaim = false;
        ((entry.gc_policy == Policies::id
            ? (handled = true, reclaim = Policies::should_reclaim(entry, now, cfg), true)
            : false), ...);
        return handled && reclaim;
    }

    template<class T> class AssetStore;
    template<class T> class AssetHandle;

    template<class T>
    class AssetRef {
    public:
        AssetRef() = default;
        explicit AssetRef(T* ptr) : ptr_(ptr) {}

        T* get() const { return ptr_; }
        T* operator->() const { return ptr_; }
        T& operator*() const { return *ptr_; }
        explicit operator bool() const { return ptr_ != nullptr; }

    private:
        T* ptr_ = nullptr;
    };

    template<class T>
    class AssetHandle {
    public:
        AssetHandle() = default;
        AssetHandle(const AssetHandle& other) : store_(other.store_), slot_(other.slot_) {
            add_ref();
        }
        AssetHandle(AssetHandle&& other) noexcept : store_(other.store_), slot_(other.slot_) {
            other.store_ = nullptr;
            other.slot_ = {};
        }
        AssetHandle& operator=(const AssetHandle& other) {
            if (this == &other) return *this;
            release();
            store_ = other.store_;
            slot_ = other.slot_;
            add_ref();
            return *this;
        }
        AssetHandle& operator=(AssetHandle&& other) noexcept {
            if (this == &other) return *this;
            release();
            store_ = other.store_;
            slot_ = other.slot_;
            other.store_ = nullptr;
            other.slot_ = {};
            return *this;
        }
        ~AssetHandle() { release(); }

        T* get(double now = 0.0) const;
        LoadState state() const;
        bool loaded() const { return state() == LoadState::Loaded; }
        bool valid() const { return get() != nullptr; }
        AssetRef<T> read() const { return AssetRef<T>(get()); }
        std::shared_ptr<T> shared() const;
        T* operator->() const { return get(); }
        T& operator*() const { return *get(); }
        explicit operator bool() const { return valid(); }

    private:
        friend class AssetStore<T>;
        struct AdoptRef {};
        AssetHandle(AssetStore<T>* store, EntrySlot slot, AdoptRef) : store_(store), slot_(slot) {}

        void add_ref();
        void release();

        AssetStore<T>* store_ = nullptr;
        EntrySlot slot_{};
    };

    template<class T>
    class WeakHandle {
    public:
        WeakHandle() = default;
        WeakHandle(AssetStore<T>* store, EntrySlot slot) : store_(store), slot_(slot) {}
        explicit WeakHandle(const AssetHandle<T>& handle) : store_(handle.store_), slot_(handle.slot_) {}

        AssetHandle<T> lock() const;

    private:
        AssetStore<T>* store_ = nullptr;
        EntrySlot slot_{};
    };

    struct IAssetStore {
        virtual AssetTypeId type_id() const = 0;
        virtual void gc(double now) = 0;
        virtual AssetStoreStats stats() const = 0;
        virtual ~IAssetStore() = default;
    };

    struct AssetManager {
        using GcPolicies = TypeList<PersistentGc, RefCountZeroGc, TimedGc, LruGc>;

        AssetManager() = default;
        AssetManager(const AssetManager&) = delete;
        AssetManager& operator=(const AssetManager&) = delete;
        AssetManager(AssetManager&&) noexcept = default;
        AssetManager& operator=(AssetManager&&) noexcept = default;

        template<class T>
        AssetStore<T>& store();

        void gc(double now) {
            for (auto& [_, store] : stores_) store->gc(now);
        }

        AssetStats stats() const {
            AssetStats out;
            out.stores.reserve(stores_.size());
            for (const auto& [_, store] : stores_) out.stores.push_back(store->stats());
            return out;
        }

    private:
        std::unordered_map<std::type_index, std::unique_ptr<IAssetStore>> stores_;
    };

    template<class T>
    struct AssetEntry {
        AssetEntryHeader header;
        std::shared_ptr<T> data;
        std::string key;
    };

    template<class T>
    class AssetStore : public IAssetStore {
    public:
        using Entry = AssetEntry<T>;

        AssetTypeId type_id() const override { return AssetTypeId(typeid(T)); }

        AssetHandle<T> acquire(AssetId id, std::string key = {}, GcPolicyId policy = GcPolicyId::Default) {
            EntrySlot slot = find_or_create(id, std::move(key), policy);
            add_ref(slot);
            return AssetHandle<T>(this, slot, typename AssetHandle<T>::AdoptRef{});
        }

        AssetHandle<T> acquire_existing(EntrySlot slot) {
            if (!pool_.get(slot)) return {};
            add_ref(slot);
            return AssetHandle<T>(this, slot, typename AssetHandle<T>::AdoptRef{});
        }

        Entry* find(AssetId id) {
            auto it = index_.find(id);
            return it == index_.end() ? nullptr : pool_.get(it->second);
        }

        const Entry* find(AssetId id) const {
            auto it = index_.find(id);
            return it == index_.end() ? nullptr : pool_.get(it->second);
        }

        bool contains(AssetId id) const { return find(id) != nullptr; }

        LoadState state(AssetId id) const {
            auto* e = find(id);
            return e ? e->header.state : LoadState::NotLoaded;
        }

        LoadState state(const AssetHandle<T>& handle) const {
            auto* e = entry_for(handle);
            return e ? e->header.state : LoadState::NotLoaded;
        }

        T* data(AssetId id) {
            auto* e = find(id);
            if (!e || e->header.state != LoadState::Loaded) return nullptr;
            e->header.last_used = now_;
            return e->data.get();
        }

        T* data(const AssetHandle<T>& handle, double = 0.0) {
            auto* e = entry_for(handle);
            if (!e || e->header.state != LoadState::Loaded) return nullptr;
            e->header.last_used = now_;
            return e->data.get();
        }

        std::shared_ptr<T> shared(const AssetHandle<T>& handle) const {
            auto* e = entry_for(handle);
            if (!e || e->header.state != LoadState::Loaded) return {};
            return e->data;
        }

        void set_policy(UnloadConfig cfg) {
            default_policy_ = to_gc_policy_id(cfg.policy);
            default_config_.ttl_sec = cfg.ttl_sec;
            default_config_.lru_budget = cfg.lru_budget;
        }

        void set_loading(AssetId id, std::string key) {
            EntrySlot slot = find_or_create(id, std::move(key), GcPolicyId::Default);
            if (auto* e = pool_.get(slot)) e->header.state = LoadState::Loading;
        }

        void set_loaded(AssetId id, std::shared_ptr<T> data) {
            EntrySlot slot = find_or_create(id, {}, GcPolicyId::Default);
            if (auto* e = pool_.get(slot)) {
                e->data = std::move(data);
                e->header.state = e->data ? LoadState::Loaded : LoadState::Failed;
            }
        }

        void set_failed(AssetId id) {
            EntrySlot slot = find_or_create(id, {}, GcPolicyId::Default);
            if (auto* e = pool_.get(slot)) e->header.state = LoadState::Failed;
        }

        void gc(double now) override {
            now_ = now;
            std::vector<EntrySlot> reclaim;

            std::size_t lru_unref = 0;
            pool_.for_each_live([&](EntrySlot, Entry& e) {
                if (e.header.strong_refs == 0 && e.header.gc_policy == GcPolicyId::Lru) ++lru_unref;
            });
            if (lru_unref > default_config_.lru_budget) {
                std::vector<EntrySlot> lru;
                pool_.for_each_live([&](EntrySlot slot, Entry& e) {
                    if (e.header.strong_refs == 0 && e.header.gc_policy == GcPolicyId::Lru) lru.push_back(slot);
                });
                std::sort(lru.begin(), lru.end(), [&](EntrySlot a, EntrySlot b) {
                    return pool_.get(a)->header.last_used > pool_.get(b)->header.last_used;
                });
                for (std::size_t i = default_config_.lru_budget; i < lru.size(); ++i)
                    if (auto* e = pool_.get(lru[i])) e->header.pending_reclaim = true;
            }

            pool_.for_each_live([&](EntrySlot slot, Entry& e) {
                if (e.header.strong_refs == 0 && e.header.released_at == 0.0) e.header.released_at = now;
                if (e.header.strong_refs != 0) {
                    e.header.released_at = 0.0;
                    e.header.pending_reclaim = false;
                }
                if (should_reclaim_by_policy(typename AssetManager::GcPolicies{}, e.header, now, default_config_))
                    reclaim.push_back(slot);
            });

            for (EntrySlot slot : reclaim) {
                if (auto* e = pool_.get(slot)) index_.erase(e->header.id);
                pool_.release(slot);
            }
        }

        AssetStoreStats stats() const override {
            AssetStoreStats s;
            s.type = type_id();
            pool_.for_each_live([&](EntrySlot, const Entry& e) {
                ++s.live_entries;
                if (e.header.state == LoadState::Loaded) ++s.loaded_entries;
                if (e.header.state == LoadState::Loading) ++s.loading_entries;
                if (e.header.strong_refs == 0) ++s.unreferenced_entries;
            });
            return s;
        }

    private:
        friend class AssetHandle<T>;
        friend class WeakHandle<T>;

        EntrySlot find_or_create(AssetId id, std::string key, GcPolicyId policy) {
            if (auto it = index_.find(id); it != index_.end()) {
                if (auto* e = pool_.get(it->second); e && !key.empty()) e->key = std::move(key);
                return it->second;
            }

            Entry entry;
            entry.header.id = id;
            entry.header.gc_policy = policy == GcPolicyId::Default ? default_policy_ : policy;
            entry.key = std::move(key);
            EntrySlot slot = pool_.allocate(std::move(entry));
            if (auto* e = pool_.get(slot)) e->header.generation = slot.generation;
            index_[id] = slot;
            return slot;
        }

        Entry* entry_for(const AssetHandle<T>& handle) {
            return handle.store_ == this ? pool_.get(handle.slot_) : nullptr;
        }

        const Entry* entry_for(const AssetHandle<T>& handle) const {
            return handle.store_ == this ? pool_.get(handle.slot_) : nullptr;
        }

        void add_ref(EntrySlot slot) {
            if (auto* e = pool_.get(slot)) {
                ++e->header.strong_refs;
                e->header.pending_reclaim = false;
                e->header.released_at = 0.0;
            }
        }

        void release_ref(EntrySlot slot) {
            if (auto* e = pool_.get(slot); e && e->header.strong_refs > 0) {
                --e->header.strong_refs;
                if (e->header.strong_refs == 0) {
                    e->header.pending_reclaim = false;
                    e->header.released_at = now_;
                }
            }
        }

        AssetEntryPool<Entry> pool_;
        std::unordered_map<AssetId, EntrySlot> index_;
        GcPolicyId default_policy_ = GcPolicyId::RefCountZero;
        GcConfig default_config_;
        double now_ = 0.0;
    };

    template<class T>
    AssetStore<T>& AssetManager::store() {
        const std::type_index key(typeid(T));
        auto it = stores_.find(key);
        if (it != stores_.end()) return *static_cast<AssetStore<T>*>(it->second.get());
        auto store = std::make_unique<AssetStore<T>>();
        auto* ptr = store.get();
        stores_[key] = std::move(store);
        return *ptr;
    }

    template<class T>
    void AssetHandle<T>::add_ref() {
        if (store_) store_->add_ref(slot_);
    }

    template<class T>
    void AssetHandle<T>::release() {
        if (store_) {
            store_->release_ref(slot_);
            store_ = nullptr;
            slot_ = {};
        }
    }

    template<class T>
    T* AssetHandle<T>::get(double now) const {
        (void)now;
        return store_ ? store_->data(*this) : nullptr;
    }

    template<class T>
    LoadState AssetHandle<T>::state() const {
        return store_ ? store_->state(*this) : LoadState::NotLoaded;
    }

    template<class T>
    std::shared_ptr<T> AssetHandle<T>::shared() const {
        return store_ ? store_->shared(*this) : std::shared_ptr<T>{};
    }

    template<class T>
    AssetHandle<T> WeakHandle<T>::lock() const {
        return store_ ? store_->acquire_existing(slot_) : AssetHandle<T>{};
    }

    template<class T>
    using Assets = AssetStore<T>;
}
