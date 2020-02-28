#pragma once

#include <filesystem>
#include <comm.hpp>

namespace gld
{
    
    enum class ResType{
        text = 0x0,
        image,
        model
    };

#ifndef PF_ANDROID
    using PathTy = std::filesystem::path;
#else
    using AndroidCxtPtrTy = std::shared_ptr<EGLCxt>;
    using PathTy = std::string;
#endif

namespace res_ck{

#ifndef PF_ANDROID
    template <class T,typename ...Args>											
    using has_load_func_t = decltype(T::load(std::declval<PathTy>(),std::declval<Args>()...));
#else
    template <class T,typename ...Args>
    using has_load_func_t = decltype(T::load(std::declval<AndroidCxtPtrTy>(),std::declval<PathTy>(),std::declval<Args>()...));
#endif
    template <typename T,typename ...Args>
    using has_load_func_vt = wws::is_detected<has_load_func_t,T,Args...>;
#ifndef PF_ANDROID
    template <class T>
    using has_load_func2_t = decltype(T::load(std::declval<PathTy>()));
#else
    template <class T>
    using has_load_func2_t = decltype(T::load(std::declval<AndroidCxtPtrTy>(),std::declval<PathTy>()));
#endif
    template <typename T>
    using has_load_func2_vt = wws::is_detected<has_load_func2_t, T>;

#ifndef PF_ANDROID
    template <class T,class ... Args>
    using has_load_func3_t = decltype(T::load(std::declval<PathTy>(),std::declval<std::tuple<Args...>>()));
#else
    template <class T,class ... Args>
    using has_load_func3_t = decltype(T::load(std::declval<AndroidCxtPtrTy>(),std::declval<PathTy>(),std::declval<std::tuple<Args...>>()));
#endif
    template <typename T,class ...Args>
    using has_load_func3_vt = wws::is_detected<has_load_func3_t, T,std::decay_t<Args> ...>;

    template <class T>											
    using has_ret_type_t = typename T::RetTy;

    template <typename T>
    using has_ret_type_vt = wws::is_detected<has_ret_type_t,T>;

     template <class T>											
    using has_args_type_t = typename T::ArgsTy;

    template <typename T>
    using has_args_type_vt = wws::is_detected<has_args_type_t,T>;

    template <class T,class Args>											
    using has_format_args_func_t = decltype(T::format_args(std::declval<Args>()));

    template <typename T,typename Args>
    using has_format_args_func_vt = wws::is_detected<has_format_args_func_t,T,Args>;

    template <class T>											
    using has_default_args_func_t = decltype(T::default_args());

    template <typename T>
    using has_default_args_func_vt = wws::is_detected<has_default_args_func_t,T>;
}

    template<ResType ty,typename T>
    struct ResLoadPlugTy
    {
        constexpr static size_t res_type = static_cast<size_t>(ty);
        using type = T;

        static_assert(res_ck::has_ret_type_vt<T>::value,"this type must has RetTy!!!");
        static_assert(res_ck::has_args_type_vt<T>::value,"this type must has ArgsTy!!!");
        
        using Ret = typename T::RetTy;
        using Args = typename T::ArgsTy;
        static_assert(
            res_ck::has_load_func_vt<T,Args>::value || (res_ck::has_load_func2_vt<T>::value && std::is_same_v<Args,void>),
            "this type must has load func!!!");
    };

    template<size_t Rt,typename ...Ts>
    struct MapResPlug;

    template<size_t Rt,typename Fir,typename ...Ts>
    struct MapResPlug<Rt,Fir,Ts...>
    {
        constexpr static decltype(auto) func()
        {
            if constexpr (Rt == Fir::res_type)
            {
                using T = typename Fir::type;
                return std::declval<T>();
            }
            else
            {
                using T = typename MapResPlug<Rt, Ts...>::type;
                if constexpr (std::is_same_v<T, void>)
                {
                    static_assert("Error Type!!!");
                }
                return std::declval<T>();
            }
        }
        using type = typename std::remove_reference_t<decltype(func())>;
    };

    template<size_t Rt>
    struct MapResPlug<Rt>
    {
        using type = void;
    };

    template<size_t Rt,size_t Idx,typename Fir,typename ...Ts>
    constexpr size_t get_res_idx_inside()
    {
        if constexpr(Rt == Fir::res_type)
        {
            return Idx;
        }else{
            if constexpr( sizeof...(Ts) > 0 )
            {
                return get_res_idx_inside<Rt,Idx + 1,Ts...>();
            }
        }
    }

    template<size_t Rt,typename ...Ts>
    constexpr size_t get_res_idx()
    {
        return get_res_idx_inside<Rt,0,Ts...>();
    }


    template<typename ...T>
    std::tuple<bool,T...> make_result(bool s,T&& ...t)
    {
        return std::make_tuple(s,std::forward<T>(t)...);
    }

} // namespace gld
