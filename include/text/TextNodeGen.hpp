#pragma once

#include <memory>
#include <node.hpp>
#include <texture.hpp>
#include <component.h>
#include <glm_uniform.hpp>

namespace txt {
	struct WordData;

	struct DefTextNodeGen {
		static std::shared_ptr<gld::Node<gld::Component>> generate(std::shared_ptr < gld::Texture<gld::TexType::D2>> tex, WordData& wd,
            int texSize, float originX = 0.5f, float originY = 0.5f);
	};

    struct DefTextMaterial :public gld::Component
    {
        DefTextMaterial(std::shared_ptr<gld::Texture<gld::TexType::D2>> diffuseTex,glm::vec4 color) :
            udiffuseTex("diffuseTex"),
            color("textColor"),
            diffuseTex(std::move(diffuseTex))
        {
            udiffuseTex = 0;
            this->color = color;
        }
        DefTextMaterial(std::shared_ptr<gld::Texture<gld::TexType::D2>> diffuseTex) :
            udiffuseTex("diffuseTex"),
            color("textColor"),
            diffuseTex(std::move(diffuseTex))
        {
            udiffuseTex = 0;
            this->color = glm::vec4(1.f, 1.f, 1.f,1.f);
        }
        bool init() override
        {
            bool f = true;

            auto n_ptr = get_node();
            auto render = n_ptr->get_comp<gld::Render>();
            udiffuseTex.attach_program(render->get());
            color.attach_program(render->get());

            return f;
        }
        void draw() override
        {
            if (diffuseTex)
                diffuseTex->active<gld::ActiveTexId::_0>();
           
            udiffuseTex.sync();
            color.sync();
        }

        void after_draw() override
        {
            diffuseTex->unbind();
        }

        gld::GlmUniform<gld::UT::Sampler2D>   udiffuseTex;
        gld::GlmUniform<gld::UT::Vec4>       color;
        std::shared_ptr<gld::Texture<gld::TexType::D2>> diffuseTex;
    };

    struct PatchTextMaterial :public gld::Component
    {
        PatchTextMaterial(std::shared_ptr<gld::Texture<gld::TexType::D2>> diffuseTex) :
            udiffuseTex("diffuseTex"),
            diffuseTex(std::move(diffuseTex))
        {
            udiffuseTex = 0;
        }
 

        PatchTextMaterial() :
            udiffuseTex("diffuseTex")
        {
            udiffuseTex = 0;
        }
        bool init() override
        {
            bool f = true;

            auto n_ptr = get_node();
            auto render = n_ptr->get_comp<gld::Render>();
            udiffuseTex.attach_program(render->get());

            return f;
        }
        void draw() override
        {
            if (diffuseTex)
                diffuseTex->active<gld::ActiveTexId::_0>();

            udiffuseTex.sync();
        }

        void after_draw() override
        {
            diffuseTex->unbind();
        }

        gld::GlmUniform<gld::UT::Sampler2D>   udiffuseTex;
        std::shared_ptr<gld::Texture<gld::TexType::D2>> diffuseTex;
    };
}