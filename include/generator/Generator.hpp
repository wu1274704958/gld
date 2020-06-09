#pragma once

#include <component.h>
#include <comps/Material.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace gld::gen{


    std::tuple<std::vector<glm::vec2>, glm::vec2> curve(float curved_x, float curved_y, int curved_seg);

    std::shared_ptr<def::Mesh> curved_surface(float w, float h, float curved_x, float curved_y, int curved_seg, bool flip_UV = false);

    template<bool HasNormal>
    std::tuple<std::vector<float>,std::vector<int>> quad(glm::vec2 lt,glm::vec2 rt,glm::vec2 lb,glm::vec2 rb)
    {
        if constexpr (HasNormal)
        {
            std::vector<float> res = {
             0.5f,   0.5f, 0.f,  0.0f,  0.0f,  -1.0f,  lt.x, lt.y, //0  
            -0.5f,   0.5f, 0.f,  0.0f,  0.0f,  -1.0f,  rt.x, rt.y, //1
            -0.5f,  -0.5f, 0.f,  0.0f,  0.0f,  -1.0f,  rb.x, rb.y, //2
             0.5f,  -0.5f, 0.f,  0.0f,  0.0f,  -1.0f,  lb.x, lb.y  //3
            };
            std::vector<int> idx = { 0,2,1,0,3,2 };

            return std::make_tuple(res, idx);
        }
        else {
            std::vector<float> res = {
             0.5f,   0.5f, 0.f, lt.x, lt.y, //0  
            -0.5f,   0.5f, 0.f, rt.x, rt.y, //1
            -0.5f,  -0.5f, 0.f, rb.x, rb.y, //2
             0.5f,  -0.5f, 0.f, lb.x, lb.y  //3
            };
            std::vector<int> idx = { 0,2,1,0,3,2 };

            return std::make_tuple(res, idx);
        }
    }

    template<bool HasNormal>
    std::tuple<std::vector<float>, std::vector<int>> quad(float x, float y,float w, float h)
    {
        return quad<HasNormal>(glm::vec2(x, y), glm::vec2(x + w, y), glm::vec2(x, y + h), glm::vec2(x + w, y + h));
    }

    
}