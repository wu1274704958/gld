#pragma once

#include <component.h>
#include <gl_comm.hpp>
#include <glm_uniform.hpp>
#include <vertex_arr.hpp>

namespace gld::def{

    struct Material:public Component
    {
        Material( std::shared_ptr<Texture<TexType::D2>> diffuseTex,
	    std::shared_ptr<Texture<TexType::D2>> specularTex):
            udiffuseTex("diffuseTex"),
            uspecularTex("specularTex"),
            uambient_strength("ambient_strength"),
            uspecular_strength("specular_strength"),
            ushininess("shininess"),
            diffuseTex(std::move(diffuseTex)),
        specularTex(std::move(specularTex))
        {
            udiffuseTex = 0;
            uspecularTex = 1;
            ushininess = 32.f;
            uspecular_strength = 0.7f;
            uambient_strength = 0.1f;
        }
        bool init() override
        {
            auto render = get_node()->get_comp<Render>();
            udiffuseTex.attach_program(render->get());
            uspecularTex.attach_program(render->get());
            uambient_strength.attach_program(render->get());
            uspecular_strength.attach_program(render->get());
            ushininess.attach_program(render->get());
        }
        void on_draw() override
        {
            diffuseTex->active<ActiveTexId::_0>();
            specularTex->active<ActiveTexId::_1>();

            uspecularTex.sync();
            udiffuseTex.sync();
            uambient_strength.sync();
            ushininess.sync();
            uspecular_strength.sync();
        }
       
        GlmUniform<UT::Sampler2D>   udiffuseTex;
        GlmUniform<UT::Sampler2D>   uspecularTex;
        GlmUniform<UT::Float>       uambient_strength;
        GlmUniform<UT::Float>       uspecular_strength;
        GlmUniform<UT::Float>       ushininess;
        std::shared_ptr<Texture<TexType::D2>> diffuseTex;
	    std::shared_ptr<Texture<TexType::D2>> specularTex;
    };

    struct Vertex{
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec2 uv;
    };

    struct Mesh : public Component
    {
        Mesh(
            int index_size,
            int vertex_size,
            std::shared_ptr<gld::VertexArr> vao)
            : index_size(index_size),
            vertex_size(vertex_size),
            vao(std::move(vao))
        {   

        }
        void on_draw() override
        {

        }
        void draw() override
        {
            vao->bind();
            if(vao->buffs().get<ArrayBufferType::ELEMENT>().good())
            {
                glDrawElements(GL_TRIANGLES,index_size,MapGlTypeEnum<unsigned int>::val,nullptr);
            }else{
                glDrawArrays(GL_TRIANGLES,0,vertex_size);
            }
		    vao->unbind();
        }
        
        int64_t idx() override { return 100;}
        int index_size;
        int vertex_size;
        std::shared_ptr<gld::VertexArr> vao;
    };
}