#include <glad/glad.h>
#include <vector>

#include <vertex_arr.hpp>

#include <ecs/render/MeshFactory.hpp>

namespace gld::ecs {

    MeshHandle MeshFactory::cube(float size) {
        // 24 vertices: pos(3) + uv(2), 6 faces.
        const float s = size * 0.5f;
        std::vector<float> v = {
            // +Z (front)
            -s,-s, s, 0,0,   s,-s, s, 1,0,   s, s, s, 1,1,  -s, s, s, 0,1,
            // -Z (back)
             s,-s,-s, 0,0,  -s,-s,-s, 1,0,  -s, s,-s, 1,1,   s, s,-s, 0,1,
            // +X (right)
             s,-s, s, 0,0,   s,-s,-s, 1,0,   s, s,-s, 1,1,   s, s, s, 0,1,
            // -X (left)
            -s,-s,-s, 0,0,  -s,-s, s, 1,0,  -s, s, s, 1,1,  -s, s,-s, 0,1,
            // +Y (top)
            -s, s, s, 0,0,   s, s, s, 1,0,   s, s,-s, 1,1,  -s, s,-s, 0,1,
            // -Y (bottom)
            -s,-s,-s, 0,0,   s,-s,-s, 1,0,   s,-s, s, 1,1,  -s,-s, s, 0,1,
        };
        std::vector<int> idx;
        for (int f = 0; f < 6; ++f) {
            int b = f * 4;
            idx.insert(idx.end(), { b, b + 1, b + 2, b, b + 2, b + 3 });
        }

        auto vao = std::make_shared<VertexArr>();
        vao->create();
        vao->create_arr<ArrayBufferType::VERTEX>();
        vao->create_arr<ArrayBufferType::ELEMENT>();
        vao->bind();
        vao->buffs().get<ArrayBufferType::VERTEX>().bind_data(v, GL_STATIC_DRAW);
        vao->buffs().get<ArrayBufferType::VERTEX>().vertex_attrib_pointer<
            VAP_DATA<3, float, false>, VAP_DATA<2, float, false>>();
        vao->buffs().get<ArrayBufferType::ELEMENT>().bind_data(idx, GL_STATIC_DRAW);
        vao->unbind();

        MeshHandle m;
        m.vao = std::move(vao);
        m.index_count = static_cast<int>(idx.size());
        m.mode = GL_TRIANGLES;
        return m;
    }

    MeshHandle MeshFactory::quad(float size) {
        const float s = size * 0.5f;
        std::vector<float> v = {
            -s,-s, 0, 0,0,   s,-s, 0, 1,0,   s, s, 0, 1,1,  -s, s, 0, 0,1,
        };
        std::vector<int> idx = { 0, 1, 2, 0, 2, 3 };

        auto vao = std::make_shared<VertexArr>();
        vao->create();
        vao->create_arr<ArrayBufferType::VERTEX>();
        vao->create_arr<ArrayBufferType::ELEMENT>();
        vao->bind();
        vao->buffs().get<ArrayBufferType::VERTEX>().bind_data(v, GL_STATIC_DRAW);
        vao->buffs().get<ArrayBufferType::VERTEX>().vertex_attrib_pointer<
            VAP_DATA<3, float, false>, VAP_DATA<2, float, false>>();
        vao->buffs().get<ArrayBufferType::ELEMENT>().bind_data(idx, GL_STATIC_DRAW);
        vao->unbind();

        MeshHandle m;
        m.vao = std::move(vao);
        m.index_count = static_cast<int>(idx.size());
        m.mode = GL_TRIANGLES;
        return m;
    }
}
