#pragma once

namespace gld{
    enum class TexType
    {
        D1 = 0x0DE0,
        D2 = 0x0DE1,
        D3 = 0x806F
    };

    enum class TexOption{
        MAG_FILTER   =      0x2800,
        MIN_FILTER   =      0x2801,
        WRAP_S       =      0x2802,
        WRAP_T       =      0x2803,
        WRAP_R       =      0x8072,
        BASE_LEVEL   =      0x813C,
        MAX_LEVEL    =      0x813D
    };

    /**
     *  GL_NEAREST	在mip基层上使用最邻近过滤
        GL_LINEAR	在mip基层上使用线性过滤
        GL_NEAREST_MIPMAP_NEAREST	选择最邻近的mip层，并使用最邻近过滤
        GL_NEAREST_MIPMAP_LINEAR	在mip层之间使用线性插值和最邻近过滤
        GL_LINEAR_MIPMAP_NEAREST	选择最邻近的mip层，使用线性过滤
        GL_LINEAR_MIPMAP_LINEAR	在mip层之间使用线性插值和使用线性过滤，又称三线性mipmap
     * **/

    /**
     * 
     *  GL_REPEAT	对纹理的默认行为。重复纹理图像。
        GL_MIRRORED_REPEAT	和GL_REPEAT一样，但每次重复图片是镜像放置的。
        GL_CLAMP_TO_EDGE	纹理坐标会被约束在0到1之间，超出的部分会重复纹理坐标的边缘，产生一种边缘被拉伸的效果。
        GL_CLAMP_TO_BORDER	超出的坐标为用户指定的边缘颜色。
     * **/
    enum class TexOpVal{
        // Mipmap过滤
        NEAREST                  =   0x2600,
        LINEAR                   =   0x2601,
        NEAREST_MIPMAP_NEAREST   =   0x2700,
        NEAREST_MIPMAP_LINEAR    =   0x2702,
        LINEAR_MIPMAP_NEAREST    =   0x2701,
        LINEAR_MIPMAP_LINEAR     =   0x2703,
        // wrap 方式 

        REPEAT                   =   0x2901,
        MIRRORED_REPEAT          =   0x8370,
        CLAMP_TO_EDGE            =   0x812F,
        CLAMP_TO_BORDER          =   0x812D
    };

    enum class ActiveTexId{
        _0      =   0x84C0,
        _1      =   0x84C1,
        _2      =   0x84C2,
        _3      =   0x84C3,
        _4      =   0x84C4,
        _5      =   0x84C5,
        _6      =   0x84C6,
        _7      =   0x84C7,
        _8      =   0x84C8,
        _9      =   0x84C9,
        _10     =   0x84CA,
        _11     =   0x84CB,
        _12     =   0x84CC,
        _13     =   0x84CD,
        _14     =   0x84CE,
        _15     =   0x84CF,
        _16     =   0x84D0,
        _17     =   0x84D1,
        _18     =   0x84D2,
        _19     =   0x84D3,
        _20     =   0x84D4,
        _21     =   0x84D5,
        _22     =   0x84D6,
        _23     =   0x84D7,
        _24     =   0x84D8,
        _25     =   0x84D9,
        _26     =   0x84DA,
        _27     =   0x84DB,
        _28     =   0x84DC,
        _29     =   0x84DD,
        _30     =   0x84DE,
        _31     =   0x84DF
    };

    struct MaxActiveTexId{
        inline static constexpr size_t val = static_cast<size_t>(ActiveTexId::_31) - static_cast<size_t>(ActiveTexId::_0);
    };

    template<TexOption Op,TexOpVal ...Vs>
    struct TexOpLimit {
        template<TexOpVal V>
        constexpr static bool can_set = can_set_sub<V,Vs...>();

        constexpr static size_t Option = static_cast<size_t>(Op);

        template<TexOpVal V,TexOpVal F,TexOpVal ...vs>
        constexpr static bool can_set_sub()
        {
            if constexpr(static_cast<size_t>(F) == static_cast<size_t>(V))
            {
                return true;
            }else
            {
                if constexpr(sizeof...(vs) == 0)
                {
                    return false;
                }else{
                    return can_set_sub<V,vs...>();
                }
            }
        }
    };

    template<typename ...Args>
    struct TexOpLimitList{
        template<TexOption Op,TexOpVal V>
        constexpr static bool can_set = can_set_sub<Op,V,Args...>();

        template<TexOption Op,TexOpVal V,typename F,typename ...args>
        constexpr static bool can_set_sub()
        {
            if constexpr(static_cast<size_t>(Op) == F::Option)
            {
                return F::template can_set<V>;
            }else
            {
                if constexpr(sizeof...(args) == 0)
                {
                    return true;
                }else{
                    return can_set_sub<Op,V,args...>();
                }
            }
        }
    };
    
}