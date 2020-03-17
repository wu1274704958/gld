#pragma once

#include <texture.hpp>

namespace gld{

    template<typename T>
    struct TexData2DWarp{
        int level;
        int internalformat;
        int border;
        unsigned int format;
        const T* pixels;
        int width;
        int height;
    };

    class CubeTexture : public Texture<TexType::CUBE>
    {
        constexpr static TexType Tt = TexType::CUBE;
    private:
    public:
        template<typename T>
        void tex_image_cube(std::initializer_list<TexData2DWarp<T>> list)
        {
            unsigned int type = static_cast<unsigned int>(MapGlTypeEnum<T>::val);
            assert(list.size() == 6);
            for(auto& c : list)
            {
                glTexImage2D(static_cast<int>(Tt),c.level,c.internalformat,c.width,c.height,c.border,c.format,type,c.pixels);
            }
        }
    };

}