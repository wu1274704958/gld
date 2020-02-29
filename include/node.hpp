#pragma once

#include <vector>
#include <memory>

namespace gld{

    template<typename Comp>
    struct Node{
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
            size_t ty_id = typeid(T);
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
                        components.insert(components.begin() + i,std::move(comp));
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
    protected:
        std::vector<std::shared_ptr<Comp>>  components;
    };
}