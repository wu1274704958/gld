#pragma once

#include <comm.hpp>

namespace gld
{
    typedef unsigned int Glid;

    enum class ShaderType{
        VERTEX = 0x8B31,
        FRAGMENT = 0x8B30,
        GEOMETRY = 0x8DD9
    };

    enum class ArrayBufferType{
        VERTEX = 0x8892,
        ELEMENT = 0x8893,
        UNIFORM = 0x8A11
    };

    enum class BufferType{
        FRAME = 0x8D40,
        RENDER = 0x8D41
    };

    template <size_t ...I>
    struct IdxArr;

    template <size_t N,size_t ...Oth>
    struct IdxArr<N,Oth...>{
        constexpr static size_t Curr = N;
        using ParTy = IdxArr<Oth...>;
    };

    template <size_t N>
    struct IdxArr<N>{
        constexpr static size_t Curr = N;
    };

    template <size_t V,size_t Idx,size_t ...I>
    constexpr size_t get_idx_inside(IdxArr<I...> ia)
    {
        if constexpr(IdxArr<I...>::Curr == V)
        {
            return Idx;
        }else{
            if constexpr(sizeof...(I) == 1)
            {
                return -1;
            }else{
                using type = typename IdxArr<I...>::ParTy;
                return get_idx_inside<V,Idx + 1>(type());
            }
        }
    }

    template <size_t V,size_t ...I>
    constexpr size_t get_idx(IdxArr<I...> ia)
    {
        if constexpr(IdxArr<I...>::Curr == V)
        {
            return 0;
        }else{
            using type = typename IdxArr<I...>::ParTy;
            return get_idx_inside<V,1>(type());
        }
    }

    template<typename T,size_t N>
    struct Pair
    {
        using type = T;
        constexpr static size_t val = N;
    };

    template<size_t V1,size_t V2>
    struct Vmv{
        constexpr static size_t v1 = V1;
        constexpr static size_t v2 = V2;
        template<size_t V>
        inline constexpr static size_t map()
        {
            if constexpr(V == V1)
            {
                return V2;
            }else
            if constexpr(V == V2)
            {
                return V1;
            }else
            {
                return 0;
            }
        }
    };
    template<typename F, typename ...VMV>
    size_t map_val_inside(size_t v)
    {
        if (F::v1 == v)
        {
            return F::v2;
        }
        if constexpr (sizeof...(VMV) > 0)
        {
            return map_val_inside<VMV...>(v);
        }
    }
    template<typename ...VMV>
    size_t map_val(std::tuple<VMV...> tup,size_t v)
    {
        if constexpr (sizeof...(VMV) > 0)
        {
            return map_val_inside<VMV...>(v);
        }
    }
   
    
    template<typename Ty>
    struct MapGlTypeEnum{
        template<size_t Idx,typename F,typename ...Ps>
        static constexpr size_t map_gl_type_enum_sub()
        {
            if constexpr (std::is_same_v<Ty, typename F::type>)
            {
                return F::val;
            }
            else {
                if constexpr (sizeof...(Ps) > 0)
                {
                    return map_gl_type_enum_sub<Idx + 1, Ps...>();
                }
                else {
                    return 0;
                }
            }
        }

        constexpr static size_t val = map_gl_type_enum_sub<0, 
                Pair<char, 0x1400>,
                Pair<unsigned char, 0x1401>,
                Pair<short, 0x1402>,
                Pair<unsigned short, 0x1403>,
                Pair<int, 0x1404>,
                Pair<unsigned int, 0x1405>,
                Pair<float, 0x1406>,
                Pair<double, 0x140A>>();
    };


    template <class T,class T2>											
    using has_need_clean_t = decltype( std::declval<T>().need_clean( std::declval<T2*>() ) );

    template <typename T,typename T2>
    using has_need_clean_vt = wws::is_detected<has_need_clean_t,T,T2>;

    template <class T,class T2>											
    using has_clean_func_t = decltype(std::declval<T>().clean( std::declval<T2*>() ) );

    template <typename T,typename T2>
    using has_clean_func_vt = wws::is_detected<has_clean_func_t,T,T2>;

    template<typename T>
    class PtrDefCleaner{
    public:
        void clean(T* t)
        {
            if(need_clean(t)) delete t;
        }
        bool need_clean(T* t)
        {
            return t != nullptr;
        }
    };

    template<typename T,template <typename T1> class Cleaner = PtrDefCleaner>
    struct PtrWarp{
        static_assert(has_need_clean_vt<Cleaner<T>,T>::value && has_clean_func_vt<Cleaner<T>,T>::value,"Must has `need_clean` and `clean` function!!");
        PtrWarp(T* t) : ptr(t) {}

        PtrWarp(const PtrWarp&) = delete;
        PtrWarp(PtrWarp&& oth){
            ptr = oth.ptr;
            oth.ptr = nullptr;
        }

        PtrWarp& operator=(const PtrWarp&) = delete;
        PtrWarp& operator=(PtrWarp&& oth){
            cleaner.clean(ptr);

            ptr = oth.ptr;
            oth.ptr = nullptr;

            return *this;
        }

        operator T*()
        {
            return ptr;
        }

        T& operator*()
        {
            return *ptr;
        }

        operator bool()
        {
            return ptr != nullptr;
        }

        T* operator->()
        {
            return ptr;
        }

        ~PtrWarp()
        {
            cleaner.clean(ptr);
        }

        T* ptr;
        Cleaner<T> cleaner;
    };
}