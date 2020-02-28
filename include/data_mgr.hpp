#pragma once

#include <data_comm.hpp>


namespace gld
{
    template<typename ...Plugs>
    class DataMgr{
    public:

        template<DataType Rt>
        auto
            load(typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type::ArgsTy args)
            ->typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type::RetTy
        {
            using Ty = typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type;
            using ARGS_T = typename Ty::ArgsTy;
            using RET_T = typename Ty::RetTy;

            auto key = Ty::key_from_args(args);

            if(ResCacheMgr<Plugs...>::instance()->template has<static_cast<size_t>(Rt)>(key))
            {
                return ResCacheMgr<Plugs...>::instance()->template get<static_cast<size_t>(Rt)>(key);
            }
#ifndef PF_ANDROID
            auto [success,res] = Ty::load(std::forward<typename Ty::ArgsTy>(args));
#else
            auto [success,res] = Ty::load(mgr,std::forward<typename Ty::ArgsTy>(args));
#endif
            if(success)
                ResCacheMgr<Plugs...>::instance()->template cache<static_cast<size_t>(Rt)>(key,res);
            return res;
        }

        template<DataType Rt>
        auto load()
            ->typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type::RetTy
        {

            static_assert(data_ck::has_default_args_func_vt<typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type>::value,
                "Load plug args type must has default_args function!!!");

            using Ty = typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type;
            using ARGS_T = typename Ty::ArgsTy;
            using RET_T = typename Ty::RetTy;

            return load<Rt>(Ty::default_args());
        }

        template<DataType Rt,typename ...Args>
        decltype(auto)
            load(Args&&... args)
        {
            using Ty = typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type;
            using ARGS_T = typename Ty::ArgsTy;
            using RET_T = typename Ty::RetTy;

            static_assert(data_ck::has_load_func3_vt<Ty,Args...>::value,"This load plug not has tuple load function!!!");

            load(std::make_tuple(std::forward<Args>(args)...));
        }

        template<DataType Rt>
        decltype(auto) rm_cache(typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type::ArgsTy args)
        {
            using Ty = typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type;

            auto key = Ty::key_from_args(args);

            return ResCacheMgr<Plugs...>::instance()->template rm_cache<static_cast<size_t>(Rt)>(key);
        }

        template<DataType Rt,typename ...Args>
        decltype(auto) rm_cache(Args&&... args)
        {
            using Ty = typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type;
            
            return rm_cache(std::make_tuple(std::forward<Args>(args)...));
        }

        template<DataType Rt>
        decltype(auto) rm_cache_def()
        {
            using Ty = typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type;
            static_assert(data_ck::has_default_args_func_vt<Ty>::value , "must has default_args function!!!");

            return rm_cache(Ty::default_args());
        }

        inline static std::shared_ptr<DataMgr<Plugs...>> instance()
        {
#ifndef PF_ANDROID
            if(!self) 
                self = std::shared_ptr<DataMgr<Plugs...>>(new DataMgr<Plugs...>());
#endif
            return self;
        }
        template<typename T>
        inline static std::shared_ptr<DataMgr<Plugs...>> create_instance(T&& t)
        {
            self = std::shared_ptr<DataMgr<Plugs...>>(new DataMgr<Plugs...>(std::forward<T>(t)));
            return self;
        }
    protected:
#ifdef PF_ANDROID
        std::shared_ptr<EGLCxt> mgr;
#endif

private:    
    inline static std::shared_ptr<DataMgr<Plugs...>> self;
#ifndef PF_ANDROID
        DataMgr<Plugs...>() {}
#else
        DataMgr<Plugs...>(std::shared_ptr<EGLCxt> mgr)
        {
            if(!mgr)
                throw std::runtime_error("EGLCxt is nullptr!!!");
            this->mgr = std::move(mgr);
        }
#endif
    };
} // namespace gld
