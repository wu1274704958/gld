#pragma once

// TreeManager – central source of truth for scene-tree STRUCTURAL changes.
//
//  It only reports structure (subtree added / removed). It does NOT track
//  property/content changes: consumers (e.g. a future batch renderer) are
//  expected to iterate their already-collected object list every frame and
//  rebuild whatever GPU state they need. TreeManager's sole job is to keep that
//  collected list's membership correct without a per-frame full-tree scan.
//
//  Usage:
//      TreeManager::instance()->install();          // hook Node structural ops
//      TreeManager::instance()->subscribe(observer);
//      TreeManager::instance()->add_root(topNode);   // fires on_subtree_added
//      ... runtime add_child / remove_child / remove_all auto-report ...

#include <node.hpp>
#include <component.h>
#include <vector>
#include <memory>

namespace gld {

    // Structural-only observer. No dirty / content callbacks by design.
    struct ITreeObserver {
        virtual void on_subtree_added(Node<Component>* root) = 0;
        virtual void on_subtree_removed(Node<Component>* root) = 0;
        virtual ~ITreeObserver() {}
    };

    class TreeManager {
    public:
        static TreeManager* instance();

        // Install/remove the global Node<Component> structural hook.
        void install();
        void uninstall();
        bool installed() const { return installed_; }

        // Register a top-level subtree (examples use a flat cxts vector, so
        // there is no single engine root). Immediately broadcasts
        // on_subtree_added so observers can collect an already-built subtree.
        void add_root(const std::shared_ptr<Node<Component>>& n);
        void remove_root(const std::shared_ptr<Node<Component>>& n);

        void subscribe(ITreeObserver* obs);
        void unsubscribe(ITreeObserver* obs);

    private:
        TreeManager() = default;

        // Forwarded from Node<Component>::s_tree_hook.
        void on_hook(Node<Component>* parent, Node<Component>* child, TreeOp op);

        void broadcast_added(Node<Component>* root);
        void broadcast_removed(Node<Component>* root);

        std::vector<ITreeObserver*> observers_;
        std::vector<std::weak_ptr<Node<Component>>> roots_;
        bool installed_ = false;
    };
}
