#pragma once

#include <component.h>
#include <comps/Material.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace gld::gen{


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

    std::shared_ptr<def::Mesh> curved_surface(float w,float h,float curved_x,float curved_y,int curved_seg,bool flip_UV = false)
    {
        float by = h / 2.f;
        float bx = -(w / 2.f);
        float ey = -by;

        std::vector<def::Vertex> vertices;

        auto [ curve_arr,curve_center ] = curve(curved_x,curved_y,curved_seg);

        float curver_off = bx;

        for(int i = static_cast<int>(curve_arr.size()) - 1;i >= 0;--i)
        {
            def::Vertex v;
            v.pos = glm::vec3( -curve_arr[i].x + bx,by,curve_arr[i].y );
            glm::vec2 p_n = glm::normalize(curve_arr[i] - curve_center);
            v.normal = glm::vec3( p_n.x,0.f,p_n.y );
            v.uv = glm::vec2((v.pos.x + 0.5f) / 1.f,flip_UV ? 0.f : 1.f);

            vertices.push_back(v);

            v.pos = glm::vec3( -curve_arr[i].x + bx,ey,curve_arr[i].y );
            p_n = glm::normalize(curve_arr[i] - curve_center);
            v.normal = glm::vec3( p_n.x,0.f,p_n.y );
            v.uv = glm::vec2((v.pos.x + 0.5f) / 1.f, flip_UV ? 1.f : 0.f);
            
            vertices.push_back(v);
        }

        bx = -bx;

        for(int i = 0;i < curve_arr.size();++i)
        {
            def::Vertex v;
            v.pos = glm::vec3( curve_arr[i].x + bx,by,curve_arr[i].y );
            glm::vec2 p_n = glm::normalize(curve_arr[i] - curve_center);
            v.normal = glm::vec3( p_n.x,0.f,p_n.y );
            v.uv = glm::vec2((v.pos.x + 0.5f) / 1.f, flip_UV ? 0.f : 1.f);

            vertices.push_back(v);

            v.pos = glm::vec3( curve_arr[i].x + bx,ey,curve_arr[i].y );
            p_n = glm::normalize(curve_arr[i] - curve_center);
            v.normal = glm::vec3( p_n.x,0.f,p_n.y );
            v.uv = glm::vec2((v.pos.x + 0.5f) / 1.f, flip_UV ? 1.f : 0.f);
            
            vertices.push_back(v);
        }
        
        auto vao = std::make_shared<VertexArr>();
        vao->create();
        vao->create_arr<ArrayBufferType::VERTEX>();
        vao->bind();
        vao->buffs().get<ArrayBufferType::VERTEX>().bind_data(vertices,GL_STATIC_DRAW);
        vao->buffs().get<ArrayBufferType::VERTEX>().vertex_attrib_pointer<
            VAP_DATA<3,float,false>,
            VAP_DATA<3,float,false>,
            VAP_DATA<2,float,false>>();
        vao->unbind();

        auto mesh = std::shared_ptr<def::Mesh>(new def::Mesh(0,vertices.size(),vao));

        return mesh;
    }

    template<bool HasNormal>
    std::tuple<std::vector<float>,std::vector<int>> quad(bool flip_UV = true)
    {

        std::vector<float> res = {
             0.5f,   0.5f, 0.f,  0.0f,  0.0f,  -1.0f,  0.0f, 0.0f,
            -0.5f,   0.5f, 0.f,  0.0f,  0.0f,  -1.0f,  0.0f, 0.0f,
            -0.5f,  -0.5f, 0.f,  0.0f,  0.0f,  -1.0f,  0.0f, 2.0f,
                                       
             0.5f,   0.5f, 0.f,  0.0f,  0.0f,  -1.0f,  2.0f, 0.0f,
            -0.5f,  -0.5f, 0.f,  0.0f,  0.0f,  -1.0f,  0.0f, 2.0f,
             0.5f,  -0.5f, 0.f,  0.0f,  0.0f,  -1.0f,  2.0f, 2.0f
        };
        std::vector<int> idx;

        return res;
        
    }

    
}