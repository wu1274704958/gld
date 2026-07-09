#include <tree/TreeManager.hpp>
#include <algorithm>

namespace gld {

    TreeManager* TreeManager::instance()
    {
        static TreeManager inst;
        return &inst;
    }

    void TreeManager::install()
    {
        if (installed_) return;
        Node<Component>::s_tree_hook =
            [this](Node<Component>* parent, Node<Component>* child, TreeOp op) {
                this->on_hook(parent, child, op);
            };
        installed_ = true;
    }

    void TreeManager::uninstall()
    {
        if (!installed_) return;
        Node<Component>::s_tree_hook = nullptr;
        installed_ = false;
    }

    void TreeManager::subscribe(ITreeObserver* obs)
    {
        if (!obs) return;
        if (std::find(observers_.begin(), observers_.end(), obs) == observers_.end())
            observers_.push_back(obs);
    }

    void TreeManager::unsubscribe(ITreeObserver* obs)
    {
        observers_.erase(std::remove(observers_.begin(), observers_.end(), obs), observers_.end());
    }

    void TreeManager::add_root(const std::shared_ptr<Node<Component>>& n)
    {
        if (!n) return;
        roots_.push_back(n);
        broadcast_added(n.get());
    }

    void TreeManager::remove_root(const std::shared_ptr<Node<Component>>& n)
    {
        if (!n) return;
        roots_.erase(std::remove_if(roots_.begin(), roots_.end(),
            [&](const std::weak_ptr<Node<Component>>& w) {
                auto sp = w.lock();
                return !sp || sp == n;
            }), roots_.end());
        broadcast_removed(n.get());
    }

    void TreeManager::on_hook(Node<Component>* /*parent*/, Node<Component>* child, TreeOp op)
    {
        if (!child) return;
        switch (op) {
        case TreeOp::ChildAdded:   broadcast_added(child);   break;
        case TreeOp::ChildRemoved: broadcast_removed(child); break;
        }
    }

    void TreeManager::broadcast_added(Node<Component>* root)
    {
        // Iterate a copy so observers may (un)subscribe within a callback.
        auto obs = observers_;
        for (auto* o : obs)
            o->on_subtree_added(root);
    }

    void TreeManager::broadcast_removed(Node<Component>* root)
    {
        auto obs = observers_;
        for (auto* o : obs)
            o->on_subtree_removed(root);
    }
}
