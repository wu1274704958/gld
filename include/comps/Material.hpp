#pragma once

#include <component.h>
#include <gl_comm.hpp>
#include <glm_uniform.hpp>
#include <vertex_arr.hpp>
#include <optional>
#include <glm/glm.hpp>
#include <glm/gtx/intersect.hpp>


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
            bool f = true;

            auto n_ptr = get_node();
            auto render = n_ptr->get_comp<Render>();
            udiffuseTex.attach_program(render->get());
            uspecularTex.attach_program(render->get());
            uambient_strength.attach_program(render->get());
            uspecular_strength.attach_program(render->get());
            ushininess.attach_program(render->get());

            return f;
        }
        void draw() override
        {
            if(diffuseTex)
                diffuseTex->active<ActiveTexId::_0>();
            if(specularTex)
                specularTex->active<ActiveTexId::_1>();

            uspecularTex.sync();
            udiffuseTex.sync();
            uambient_strength.sync();
            ushininess.sync();
            uspecular_strength.sync();
        }

        void after_draw() override
        {
            diffuseTex->unbind();
            specularTex->unbind();
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
            size_t index_size,
            size_t vertex_size,
            std::shared_ptr<gld::VertexArr> vao)
            : index_size(index_size),
            vertex_size(vertex_size),
            vao(std::move(vao)),
            mode(GL_TRIANGLES)
        {   
            
        }
        void draw() override
        {
            vao->bind();
            if(vao->buffs().get<ArrayBufferType::ELEMENT>().good())
            {
                glDrawElements(mode,static_cast<GLsizei>(index_size),MapGlTypeEnum<unsigned int>::val,nullptr);
            }else{
                glDrawArrays(mode,0,static_cast<GLsizei>(vertex_size));
            }
		    vao->unbind();
        }
        
        int64_t idx() override { return 100;}
        size_t index_size;
        size_t vertex_size;
        std::shared_ptr<gld::VertexArr> vao;
        GLenum mode;
    };

    struct MeshRayTest : public Component
    {
        MeshRayTest(
            size_t index_size,
            size_t vertex_size,
            std::shared_ptr<gld::VertexArr> vao,
            std::shared_ptr<std::vector<float>> vertices,
            size_t off_vertex,
            std::shared_ptr<std::vector<int>> indices = nullptr
            )
            : index_size(index_size),
            vertex_size(vertex_size),
            vao(std::move(vao)),
            vertices(std::move(vertices)),
            indices(std::move(indices)),
            off_vertex(off_vertex),
            mode(GL_TRIANGLES)
        {

        }
        void draw() override
        {
            vao->bind();
            if (vao->buffs().get<ArrayBufferType::ELEMENT>().good())
            {
                glDrawElements(mode, static_cast<GLsizei>(index_size), MapGlTypeEnum<unsigned int>::val, nullptr);
            }
            else {
                glDrawArrays(mode, 0, static_cast<GLsizei>(vertex_size));
            }
            vao->unbind();
        }

        void triangle(int idx, glm::vec4* v)
        {
            if (indices)
            {
                for (int i = 0; i < 3; ++i)
                {
                    float* tt = &(*vertices)[(*indices)[((idx * 3) + i)] * (3 + off_vertex)];
                    v[i].x = tt[0]; v[i].y = tt[1]; v[i].z = tt[2]; v[i].w = 1.f;
                }
            }
            else {
                for (int i = 0; i < 3; ++i)
                {
                    float* tt = &(*vertices)[idx * (3 + off_vertex) + i];
                    v[i].x = tt[0]; v[i].y = tt[1]; v[i].z = tt[2]; v[i].w = 1.f;
                }
            }
        }

        bool ray_test(glm::mat4 const& view,glm::vec3 const& pos, glm::vec3 const& dir,glm::vec2 &barypos,float& distance)
        {
            auto n_ptr = get_node();
            auto trans = n_ptr->get_comp<Transform>();
            if (!trans)
                return false;

            glm::mat4 model = trans->get_model();

            for (int i = 0; i < index_size / 3; ++i)
            {
                glm::vec4 vs[3];
                triangle(i, vs);

                glm::vec3 v1 = view * model * vs[0];
                glm::vec3 v2 = view * model * vs[1];
                glm::vec3 v3 = view * model * vs[2];
                if (glm::intersectRayTriangle(pos, dir, v1, v2, v3, barypos, distance))
                    return true;
            }
            return false;
        }

        int64_t idx() override { return 100; }

        size_t index_size;
        size_t vertex_size;
        size_t off_vertex;
        GLenum mode;
        std::shared_ptr<gld::VertexArr> vao;
        std::shared_ptr<std::vector<float>> vertices;
        std::shared_ptr<std::vector<int>> indices;
    };

    struct MeshInstanced : public Component
    {
        MeshInstanced(
            size_t index_size,
            size_t vertex_size,
            size_t count,
            std::shared_ptr<gld::VertexArr> vao)
            : index_size(index_size),
            vertex_size(vertex_size),
            instance_count(count),
            vao(std::move(vao)),
            mode(GL_TRIANGLES)
        {   

        }

        static std::shared_ptr<MeshInstanced> create_with_mesh(Mesh* m,size_t count);

        void draw() override
        {
            vao->bind();
            if(vao->buffs().get<ArrayBufferType::ELEMENT>().good())
            {   
                glDrawElementsInstanced(mode,static_cast<GLsizei>(index_size),MapGlTypeEnum<unsigned int>::val,nullptr,static_cast<GLsizei>(instance_count));
            }else{
                glDrawArraysInstanced(mode,0,static_cast<GLsizei>(vertex_size),static_cast<GLsizei>(instance_count));
            }
		    vao->unbind();
        }
        
        int64_t idx() override { return 100;}
        size_t index_size;
        size_t vertex_size;
        size_t instance_count;
        std::shared_ptr<gld::VertexArr> vao;
        GLenum mode;
    };

    struct Skybox : public Component{
        Skybox(std::shared_ptr<Texture<TexType::CUBE>> skyboxTex) : 
            uskybox("skybox"),
            skyboxTex(std::move(skyboxTex))
        {
            uskybox = 0;
        }
        bool init() override
        {
            auto n_ptr = get_node();
            auto render = n_ptr->get_comp<Render>();
            uskybox.attach_program(render->get());
            return true;
        }
        void draw() override
        {
            if(skyboxTex)
                skyboxTex->active<ActiveTexId::_0>();

            uskybox.sync();
        }
        GlmUniform<UT::SamplerCube> uskybox;
        std::shared_ptr<Texture<TexType::CUBE>> skyboxTex;
    };
}