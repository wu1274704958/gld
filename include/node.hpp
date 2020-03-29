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
            const std::type_info& ty_id = typeid(T);
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
                comp->attach_node(weak_ptr());
                components.push_back(std::move(comp));
            }else{
                for(int i = 0;i < components.size();++i)
                {
                    if(comp->idx() <= components[i]->idx())
                    {
                        comp->attach_node(weak_ptr());
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
                        return true;
                    }
                }
            }
            return false;
        }
        template<typename T>
        bool remove_comp(T * comp)
        {
            if(!comp) return false;
            const type_info& ty_id = typeid(T);
            for(int i = 0;i < components.size();++i)
            {
                if(comp_ty_id(i) == ty_id && comp == components[i].get())
                {
                    auto it = components.erase(components.begin() + i);
                    if(it != components.end())
                    {
                        return true;
                    }
                }
            }
            return false;
        }
        const std::type_info& comp_ty_id(int idx)
        {
            return typeid(*(components[idx]));
        }
        bool good_comp_idx(int idx)
        {
            return components.size() > idx;
        }
        bool good_child_idx(int idx)
        {
            return children.size() > idx;
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
                ch->clear_parent();
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
        std::weak_ptr<Node<Comp>> weak_ptr() 
        {
            return std::enable_shared_from_this<Node<Comp>>::weak_from_this();
        }
        bool init()
        {
            bool res = true;
            for(auto &comp : components)
                res = comp->init();
            for(auto &ch : children)
                res = ch->init();
            return res;
        }
      
        void draw()
        {
            if(visible)
            {
                for(auto &comp : components) 
                    comp->before_draw();
                for(auto &comp : components) 
                    comp->draw();
                for(int64_t i = static_cast<int64_t>(components.size() - 1);i >= 0;--i)
                    components[i]->after_draw();
                for(auto &ch : children)
                    ch->draw();
            }
        }
        void update()
        {
            for(auto &comp : components)
                comp->update();
            for(auto &ch : children)
                ch->update();
        }
        std::shared_ptr<Node<Comp>> get_child(uint64_t idx)
        {
            if(good_child_idx(idx))
            {
                return children[idx];
            }
            return std::shared_ptr<Node<Comp>>();
        }
        uint64_t children_count()
        {
            return static_cast<uint64_t>(children.size());
        }
        uint64_t comp_count()
        {
            return static_cast<uint64_t>(components.size());
        }
        bool visible = true;
    protected:
        void clear_parent()
        {
            parent = std::weak_ptr<Node<Comp>>();
        }

    protected:
        std::vector<std::shared_ptr<Comp>>  components;
        std::vector<std::shared_ptr<Node<Comp>>> children;
        std::weak_ptr<Node<Comp>> parent;
    };
}