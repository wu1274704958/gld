#pragma once

namespace gld
{
    typedef unsigned int Glid;

    enum class ShaderType{
        VERTEX = 0x8B31,
        FRAGMENT = 0x8B30,
        GEOMETRY = 0x8DD9
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

}