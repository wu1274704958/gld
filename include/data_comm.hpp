#pragma once

#include <res_comm.hpp>
#include <comm.hpp>

namespace gld{
    enum class DataType{
        Program = 0x100,
        Texture2D,
        Scene,
        SceneNoMaterial,
        TextureCube,
        ProgramWithGeometry,
        SceneWithGeometry,
        SceneWithGeometryNoMaterial,
        FontLibrary,
        FontFace,
        SquareVertices,
        SquareIndices
    };

    namespace data_ck{


    template <class T,typename ...Args>											
    using has_load_func_t = decltype(T::load(std::declval<Args>()...));

    template <typename T,typename ...Args>
    using has_load_func_vt = wws::is_detected<has_load_func_t,T,Args...>;


    template <class T>
    using has_load_func2_t = decltype(T::load());

    template <typename T>
    using has_load_func2_vt = wws::is_detected<has_load_func2_t, T>;


    template <class T,class ... Args>
    using has_load_func3_t = decltype(T::load( std::declval<std::tuple<Args...>>() ));

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

    template <class T,class Args>											
    using has_key_from_args_func_t = decltype(T::key_from_args(std::declval<Args>()));

    template <typename T,typename Args>
    using has_key_from_args_func_vt = wws::is_detected<has_key_from_args_func_t,T,Args>;

    }


    template<DataType ty,typename T>
    struct DataLoadPlugTy
    {
        constexpr static size_t res_type = static_cast<size_t>(ty);
        using type = T;

        static_assert(data_ck::has_ret_type_vt<T>::value,"this type must has RetTy!!!");
        static_assert(data_ck::has_args_type_vt<T>::value,"this type must has ArgsTy!!!");
        
        
        using Ret = typename T::RetTy;
        using Args = typename T::ArgsTy;
        static_assert(
            data_ck::has_load_func_vt<T,Args>::value || (data_ck::has_load_func2_vt<T>::value && std::is_same_v<Args,void>),
            "this type must has load func!!!");

        static_assert(data_ck::has_key_from_args_func_vt<T,Args>::value,"this type must has key_from_args function!!!");
    };
}