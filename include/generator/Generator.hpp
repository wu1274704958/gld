#pragma once

#include <comps/Material.hpp>

namespace gld::gen{
    std::shared_ptr<def::Mesh> curved_surface(float w,float h,float curved_x,float curved_y,int curved_seg)
    {
        return nullptr;
    }

    std::tuple<std::vector<glm::vec2>,glm::vec2> curve(float curved_x,float curved_y,int curved_seg)
    {
        glm::vec2 c(0.f,-curved_y);
        float m = glm::pi<float>() / 2.f;
        std::vector<glm::vec2> res;
        for(int i = 0;i < curved_seg;++i)
        {
            float angle = (float)i / (float)curved_seg * m;
            //float hypot = curved_y ;//+ ((curved_x - curved_y) * ((float)i / (float)curved_seg * 1.f));
            res.push_back( glm::vec2( glm::sin(angle) * curved_x, (glm::cos(angle) * curved_y) - curved_y ));
        }
        return std::make_tuple(res,c);
    }
}