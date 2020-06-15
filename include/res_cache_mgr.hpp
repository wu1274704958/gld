#pragma once

#include <res_comm.hpp>
#include <unordered_map>
#include <tuple>
#include <string>

namespace gld{

    template<typename T,typename Key = std::string>
    struct ResCache{

        using CacheTy = typename T::RetTy;

        bool has(const Key& k)
        {
            return map.find(k) != map.end();
        }

        bool has(Key&& k)
        {
            return has(k);
        }

        CacheTy get(Key& k)
        {
            if(!has(k))
                throw std::runtime_error("Not cached this key!!!");
            return map[k];
        }

        void cache(Key& k,CacheTy t)
        {
            map[k] = t;
        }

        bool rm_cache(Key& k)
        {
            if(has(k))
            {
                return map.erase(k) >= 1;
            }
            return false;
        }

        void clear_all()
        {
            map.clear();
        }

        std::unordered_map<Key,CacheTy> map;

        inline static std::shared_ptr<ResCache<T,Key>> instance()
        {
            if(!self) self = std::shared_ptr<ResCache<T,Key>>(new ResCache<T,Key>());
            return self;
        }
    private:    
        inline static std::shared_ptr<ResCache<T,Key>> self;
        ResCache<T,Key>(){}
    };

    template<typename ... Plugs>
    struct ResCacheMgr{


        template<size_t Rt,typename K>
        bool has(K&& k)
        {
            return ResCache<typename MapResPlug<Rt,Plugs...>::type>::instance()->has(std::forward<K>(k)); //std::get<get_res_idx<Rt,Plugs...>()>(tup).has(std::forward<K>(k));
        }

        template<size_t Rt,typename Key>
        decltype(auto) get(Key&& k)
        {
            return ResCache<typename MapResPlug<Rt,Plugs...>::type>::instance()->get(std::forward<Key>(k));//std::get<get_res_idx<Rt,Plugs...>()>(tup).get(std::forward<Key>(k));
        }

        template<size_t Rt,typename Key,typename T>
        void cache(Key&& k,T&& t)
        {
            //std::get<get_res_idx<Rt,Plugs...>()>(tup).cache(std::forward<Key>(k),std::forward<T>(t));
            ResCache<typename MapResPlug<Rt,Plugs...>::type>::instance()->cache(std::forward<Key>(k),std::forward<T>(t));
        }

        template<size_t Rt,typename Key>
        decltype(auto) rm_cache(Key&& k)
        {
            return ResCache<typename MapResPlug<Rt,Plugs...>::type>::instance()->rm_cache(std::forward<Key>(k));
        }

        void clear_all()
        {
            (ResCache<typename Plugs::type>::instance()->clear_all(),...);
        }

        //std::tuple<ResCache<typename Plugs::Ret> ...> tup;

        inline static std::shared_ptr<ResCacheMgr<Plugs...>> instance()
        {
            if(!self) self = std::shared_ptr<ResCacheMgr<Plugs...>>(new ResCacheMgr<Plugs...>());
            return self;
        }
    private:    
        inline static std::shared_ptr<ResCacheMgr<Plugs...>> self;
        ResCacheMgr<Plugs...>(){}
    };

    template<typename T,char ...CS>
    struct DefCache{
        using RetTy = T;
    };
}