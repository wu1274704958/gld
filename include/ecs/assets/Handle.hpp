#pragma once

// Handle<T> / WeakHandle<T> – references into an Assets<T> cache. A strong
// Handle keeps a ref-count token alive; when the last Handle to an id is
// destroyed the entry becomes unreferenced and the unload policy applies.

#include <memory>
#include "Assets.hpp"

namespace gld::ecs {

    template<class T>
    struct Handle {
        Assets<T>* owner = nullptr;
        AssetId id = 0;
        std::shared_ptr<void> token;   // keeps the cache entry "referenced"

        T* get(double now = 0.0) const { return owner ? owner->data(id) : nullptr; }
        LoadState state() const { return owner ? owner->state(id) : LoadState::NotLoaded; }
        bool valid() const { return get() != nullptr; }
        T* operator->() const { return get(); }
        T& operator*() const { return *get(); }
        explicit operator bool() const { return valid(); }
    };

    template<class T>
    struct WeakHandle {
        Assets<T>* owner = nullptr;
        AssetId id = 0;

        Handle<T> lock() const {
            if (!owner) return {};
            return Handle<T>{ owner, id, owner->acquire_token(id) };
        }
    };
}
