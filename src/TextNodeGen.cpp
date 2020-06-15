#include <text/TextNodeGen.hpp>
#include <component.h>
#include <data_mgr.hpp>
#include <generator/Generator.hpp>
#include <text/Page.hpp>
#include <comps/Material.hpp>
#include <res_cache_mgr.hpp>

using namespace gld;


std::shared_ptr<gld::Node<gld::Component>> txt::DefTextNodeGen::generate(std::shared_ptr<gld::Texture<gld::TexType::D2>> tex, WordData& wd,
    int texSize,float originX,float originY)
{
    auto program = DefDataMgr::instance()->load<DataType::Program>("base/word_vs.glsl", "base/word_fg.glsl");
    if (!program) return nullptr;
    if (program->uniform_id("perspective") == -1)
        program->locat_uniforms("perspective", "world", "model", "diffuseTex", "textColor");
    
    float tex_size = static_cast<float>(texSize);
    float x = static_cast<float>(wd.x);
    float y = static_cast<float>(wd.y);
    float w = static_cast<float>(wd.w);
    float h = static_cast<float>(wd.h);

    auto [vertices, indices] = gen::quad<false>(x / tex_size, y / tex_size, w / tex_size, h / tex_size,originX,originY);


    std::shared_ptr<gld::Node<gld::Component>> res = std::make_shared<gld::Node<gld::Component>>();

    res->add_comp<gld::Render>(std::shared_ptr<gld::Render>(new gld::Render("base/word_vs.glsl", "base/word_fg.glsl")));
    res->add_comp<gld::Transform>(std::make_shared<gld::Transform>());

    auto vao = std::make_shared<gld::VertexArr>();
    vao->create();
    vao->create_arr<gld::ArrayBufferType::VERTEX>();
    vao->create_arr<gld::ArrayBufferType::ELEMENT>();
    vao->bind();
    vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(vertices, GL_STATIC_DRAW);
    vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
        gld::VAP_DATA<3, float, false>,
        gld::VAP_DATA<2, float, false>>();
    vao->buffs().get<gld::ArrayBufferType::ELEMENT>().bind_data(indices, GL_STATIC_DRAW);
    vao->unbind();

    std::shared_ptr<std::vector<float>> vertices_;
    std::shared_ptr<std::vector<int>> indices_;

    using CacheMgr = typename gld::ResCache < DefCache<std::shared_ptr<std::vector<float>>, 'W', 'O', 'R', 'D'>>;
    std::string v_k("No Normal Vertices");
    std::string i_k("Indices");


    if (gld::ResCache < DefCache<std::shared_ptr<std::vector<float>>, 'W', 'O', 'R', 'D'>>::instance()->has(v_k))
        vertices_ = gld::ResCache < DefCache<std::shared_ptr<std::vector<float>>, 'W', 'O', 'R', 'D'>>::instance()->get(v_k);
    else {
        vertices_ = std::shared_ptr<std::vector<float>>(new std::vector<float>(vertices));
        gld::ResCache < DefCache<std::shared_ptr<std::vector<float>>, 'W', 'O', 'R', 'D'>>::instance()->cache(v_k, vertices_);
    }
        
    if (gld::ResCache < DefCache<std::shared_ptr<std::vector<int>>, 'W', 'O', 'R', 'D'>>::instance()->has(i_k))
        indices_ = gld::ResCache < DefCache<std::shared_ptr<std::vector<int>>, 'W', 'O', 'R', 'D'>>::instance()->get(i_k);
    else {
        indices_ = std::shared_ptr<std::vector<int>>(new std::vector<int>(indices));
        gld::ResCache < DefCache<std::shared_ptr<std::vector<int>>, 'W', 'O', 'R', 'D'>>::instance()->cache(i_k, indices_);
    }
    

    res->add_comp<gld::def::MeshRayTest>(std::shared_ptr<gld::def::MeshRayTest>(new gld::def::MeshRayTest(indices.size(), vertices.size() / 5, std::move(vao),
        vertices_,
        2,
        indices_
            )));

    res->add_comp<DefTextMaterial>(std::shared_ptr<DefTextMaterial>(new DefTextMaterial(std::move(tex))));

    return res;
}
