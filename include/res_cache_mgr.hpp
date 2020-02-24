#pragma once

#include <res_comm.hpp>
#include <unordered_map>
#include <tuple>
#include <string>

namespace gld{

    template<typename T,typename Key = std::string>
    struct ResCache{

        bool has(const Key& k)
        {
            return map.find(k) != map.end();
        }

        bool has(Key&& k)
        {
            return has(k);
        }

        T get(Key& k)
        {
            if(!has(k))
                throw std::runtime_error("Not cached this key!!!");
            return map[k];
        }

        void cache(Key& k,T t)
        {
            map[k] = t;
        }

        std::unordered_map<Key,T> map;
    };

    template<typename ... Plugs>
    struct ResCacheMgr{


        template<size_t Rt,typename K>
        bool has(K&& k)
        {
            return std::get<get_res_idx<Rt,Plugs...>()>(tup).has(std::forward<K>(k));
        }

        template<size_t Rt,typename Key>
        decltype(auto) get(Key&& k)
        {
            return std::get<get_res_idx<Rt,Plugs...>()>(tup).get(std::forward<Key>(k));
        }

        template<size_t Rt,typename Key,typename T>
        void cache(Key&& k,T&& t)
        {
            std::get<get_res_idx<Rt,Plugs...>()>(tup).cache(std::forward<Key>(k),std::forward<T>(t));
        }

        std::tuple<ResCache<typename Plugs::Ret> ...> tup;

        inline static std::shared_ptr<ResCacheMgr<Plugs...>> instance()
        {
            if(!self) self = std::shared_ptr<ResCacheMgr<Plugs...>>(new ResCacheMgr<Plugs...>());
            return self;
        }
    private:    
        inline static std::shared_ptr<ResCacheMgr<Plugs...>> self;
    };
}