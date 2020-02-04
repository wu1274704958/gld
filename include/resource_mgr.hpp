#pragma once

#include <filesystem>
#include <comm.hpp>


namespace gld{

    enum class ResType{
        text = 0x0,
        image,
        model
    };

    template <class T,typename ...Args>											
    using has_load_func_t = decltype(T::load(std::declval<Args>()...));

    template <typename T,typename ...Args>
    using has_load_func_vt = wws::is_detected<has_load_func_t,T,Args...>;

    template <class T>											
    using has_ret_type_t = typename T::RetTy;

    template <typename T>
    using has_ret_type_vt = wws::is_detected<has_ret_type_t,T>;

     template <class T>											
    using has_args_type_t = typename T::ArgsTy;

    template <typename T>
    using has_args_type_vt = wws::is_detected<has_args_type_t,T>;

    template<ResType ty,typename T>
    struct ResLoadPlugTy
    {
        constexpr static size_t res_type = static_cast<size_t>(ty);
        using type = T;

        static_assert(has_ret_type_vt<T>::value,"this type must has RetTy!!!");
        static_assert(has_args_type_vt<T>::value,"this type must has ArgsTy!!!");
        
        using Ret = typename T::RetTy;
        using Args = typename T::ArgsTy;
        static_assert(has_load_func_vt<T,Args>::value,"this type must has load func!!!");
        static_assert(std::is_same_v<has_load_func_t<T,Args>,Ret>,"this loadret type must same!!!");
        
    };
    

    class ResourceMgr{
    public:

    protected:
    };
}