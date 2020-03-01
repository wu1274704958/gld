#pragma once

#include <vector>
#include <memory>
#include <algorithm>

namespace gld{

    template<typename Comp>
    struct Node : public std::enable_shared_from_this<Node<Comp>>
    {
        template<typename T>
        T* get_comp()
        {
            size_t ty_id = typeid(T).hash_code();
            for(int i = 0;i < components.size();++i)
            {
                if(comp_ty_id(i) == ty_id)
                    return dynamic_cast<T*>(components[i].get()); 
            }
            return nullptr;
        }
        template<typename T>
        bool add_comp(std::shared_ptr<T> comp)
        {
#ifdef GLD_ADD_COMPONENTS_CK_REPEAT
            size_t ty_id = typeid(T).hash_code();
            for(int i = 0;i < components.size();++i)
            {
                if(comp_ty_id(i) == ty_id)
                    return false;
            }
#endif
            if(components.empty() || components.back()->idx() <= comp->idx())
            {
                components.push_back(std::move(comp));
            }else{
                for(int i = 0;i < components.size();++i)
                {
                    if(comp->idx() <= components[i]->idx())
                    {
                        components.attach_node(weak_ptr());
                        components.insert(components.begin() + i,std::move(comp));
                        return true;
                    }
                }
            }
            return false;
        }
        template<typename T>
        bool remove_comp(std::shared_ptr<T> comp)
        {
            if(!comp) return false;
            size_t ty_id = typeid(T).hash_code();
            for(int i = 0;i < components.size();++i)
            {
                if(comp_ty_id(i) == ty_id && dynamic_cast<Comp*>(comp.get()) == components[i].get())
                {
                    auto it = components.erase(components.begin() + i);
                    if(it != components.end())
                    {
                        (*it)->reset_node();
                        return true;
                    }
                }
            }
            return false;
        }
        size_t comp_ty_id(int idx)
        {
            if(good_comp_idx(idx))
                return typeid(*(components[idx])).hash_code();
            return -1;
        }
        bool good_comp_idx(int idx)
        {
            return components.size() > idx;
        }
        std::shared_ptr<Node<Comp>> get_parent()
        {
            return parent.lock();
        }
        bool add_child(std::shared_ptr<Node<Comp>> ch)
        {
            if(ch)
            {
                if(auto pare = parent.lock();pare && pare == ch)
                {
                    return false;
                }
                bool independent = false;
                if(ch->has_parent())
                    independent = ch->get_parent()->remove_child(ch);
                else
                    independent = true;
                if(independent)
                {
                    ch->parent = weak_ptr();
                    children.push_back(ch);
                    return true;
                }
            }
            return false;
        }

        bool remove_child(std::shared_ptr<Node<Comp>> ch)
        {
            if(auto it = std::find(children.begin(),children.end(),ch);it != children.end())
            {
                auto child = children.erase(it);
                (*child)->clear_parent();
                return true;
            }
            return false;
        }

        bool has_parent()
        {
            auto pare = parent.lock();
            return (bool)pare;
        }

        std::shared_ptr<Node<Comp>> ptr() 
        {
            return std::enable_shared_from_this<Node<Comp>>::shared_from_this();
        }
        std::shared_ptr<Node<Comp>> weak_ptr() 
        {
            return std::enable_shared_from_this<Node<Comp>>::weak_from_this();
        }
    protected:
        void clear_parent()
        {
            parent.reset();
        }

    protected:
        std::vector<std::shared_ptr<Comp>>  components;
        std::vector<std::shared_ptr<Node<Comp>>> children;
        std::weak_ptr<Node<Comp>> parent;
    };
}