#pragma once

#include <component.h>
#include <comps/Material.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>

namespace gld::gen{


    std::tuple<std::vector<glm::vec2>, glm::vec2> curve(float curved_x, float curved_y, int curved_seg);

    std::shared_ptr<def::Mesh> curved_surface(float w, float h, float curved_x, float curved_y, int curved_seg, bool flip_UV = false);

    template<bool HasNormal>
    std::tuple<std::vector<float>, std::vector<int>> quad(glm::vec2 lt, glm::vec2 rt, glm::vec2 lb, glm::vec2 rb, glm::vec2 origin = glm::vec2(0.5f,0.5f))
    {
        
        if constexpr (HasNormal)
        {
            std::vector<float> res = {
             origin.x,          origin.y,               0.f,        0.0f,  0.0f,  -1.0f,  lt.x, lt.y, //0  
             origin.x - 1.f,    origin.y,               0.f,        0.0f,  0.0f,  -1.0f,  rt.x, rt.y, //1
             origin.x - 1.f,    origin.y - 1.f,         0.f,        0.0f,  0.0f,  -1.0f,  rb.x, rb.y, //2
             origin.x,          origin.y - 1.f,         0.f,        0.0f,  0.0f,  -1.0f,  lb.x, lb.y  //3
            };
            std::vector<int> idx = { 0,1,2,0,2,3 };

            return std::make_tuple(std::move(res), std::move(idx));
        }
        else {
            std::vector<float> res = {
             origin.x,       origin.y,               0.f,  lt.x, lt.y, //0  
             origin.x - 1.f, origin.y,               0.f,  rt.x, rt.y, //1
             origin.x - 1.f, origin.y - 1.f,         0.f,  rb.x, rb.y, //2
             origin.x,       origin.y - 1.f,         0.f,  lb.x, lb.y  //3
            };
            std::vector<int> idx = { 0,1,2,0,2,3 };

            return std::make_tuple(std::move(res), std::move(idx));
        }
    }

    inline std::vector<float> quad(glm::vec2 origin = glm::vec2(0.5f, 0.5f))
    {
        std::vector<float> res = {
            origin.x,       origin.y,       0.f,
            origin.x - 1.f, origin.y,       0.f,
            origin.x - 1.f, origin.y - 1.f, 0.f,
            origin.x,       origin.y - 1.f, 0.f
        };

        return res;
    }

    template<bool HasNormal>
    std::tuple<std::vector<float>, std::vector<int>> quad(float x, float y, float w, float h, float originX = 0.5f, float originY = 0.5f, bool flipUV = true)
    {
        if(flipUV)
            return quad<HasNormal>( glm::vec2(x + w, y), glm::vec2(x, y), glm::vec2(x + w, y + h),glm::vec2(x, y + h) ,glm::vec2(originX,originY));
        else
            return quad<HasNormal>( glm::vec2(x, y), glm::vec2(x + w, y), glm::vec2(x, y + h) , glm::vec2(x + w, y + h), glm::vec2(originX, originY));
    }


    inline std::vector<glm::vec3> circle(float y,float r,int seg,float x0 = 0.f,float y0 = 0.f)
    {
        float m = glm::pi<float>() * 2.f;
        std::vector<glm::vec3> res;
        for(int i = 0;i < seg;++i)
        { 
            float angle = (float)i / (float)seg * m;
            res.push_back(glm::vec3(glm::sin(angle) * r + x0, y, glm::cos(angle) * r + y0));
        }
        return res;
    }

    inline std::tuple<std::vector<float>, std::vector<int>> cube()
    {
        std::vector<float > vertices = {
            0.5f, 0.5f, -0.5f,
           -0.5f, 0.5f, -0.5f,
            0.5f,-0.5f, -0.5f,
           -0.5f,-0.5f, -0.5f,
            0.5f, 0.5f,  0.5f,
           -0.5f, 0.5f,  0.5f,
            0.5f,-0.5f,  0.5f,
           -0.5f,-0.5f,  0.5f
        };

        std::vector<int> indices = {
            //front
            0,2,1,
            1,2,3,
            //top
            4,0,1,
            4,1,5,
            //bottom
            6,2,3,
            6,3,7,
            //left
            4,6,2,
            4,2,0,
            //right
            1,3,7,
            1,7,5,
            //back
            5,7,6,
            5,6,4
        };

        return { vertices,indices };
    }

    inline std::vector<glm::vec3> sphere(float r,int seg,int level,float offset = 0.0f)
    {
        float perimeter = 2.f * glm::pi<float>() * r;
        float dist = perimeter / (float)seg;
        float y_axis = 0.f;
        float y_axis_unit = (r * 2.f) / (float)(level - 1);
        float angle = 0.f;
        std::vector<glm::vec3> result;

        for(int i = 0;i < level;++i)
        {
            float rr = 0.f;
            if(y_axis > r)
            {
                float a = y_axis - r;
                rr = glm::sqrt( glm::pow(r, 2.0f) - glm::pow(a, 2.f) );
            }else
            if(y_axis == r)
            {
                rr = r;
            }else{
                float a = r - y_axis;
                rr = glm::sqrt( glm::pow(r, 2.0f) - glm::pow(a, 2.f) );
            }
            float per = 2.f * glm::pi<float>() * rr;
            int seg = (int)glm::floor(per / dist);
            if (seg > 0)
            {
                auto res = circle(y_axis - r, rr, seg);
                if(offset == 0.0f)
                    result.insert(result.end(), res.begin(), res.end());
                else {
                    for (auto& v : res)
                        result.push_back(glm::rotateY(v, angle));
                }
            }
            y_axis += y_axis_unit;
            angle += offset;
        }
        return result;
    }
}