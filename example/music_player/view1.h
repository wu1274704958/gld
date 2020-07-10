#pragma once

#include <generator/Generator.hpp>
#include "fft_view.h"

namespace gld {
	struct View1 : public FFTView {
		struct Vertex
		{
			glm::vec3 pos;
			glm::vec3 color;
		};

		void create()
		{
			perpare_vertices();
			add_comp(std::make_shared<Transform>());

			auto render = std::shared_ptr<Render>(new Render("base/base2_vs.glsl", "base/base2_fg.glsl"));
			render->init();
			auto program = render->get();
			if (program->uniform_id("perspective") == -1)
				program->locat_uniforms("perspective", "world","model");
			add_comp<Render>(render);

			auto vao = std::make_shared<gld::VertexArr>();
			vao->create();
			vao->create_arr<gld::ArrayBufferType::VERTEX>();
			vao->bind();
			vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(vertices, GL_STATIC_DRAW);
			vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
				gld::VAP_DATA<3, float, false>, gld::VAP_DATA<3, float, false>>();
			vao->unbind();
			auto mesh = std::shared_ptr<def::Mesh>(new def::Mesh(
				0, vertices.size(), std::move(vao)
			));
			mesh->mode = GL_POINTS;
			add_comp<def::Mesh>(mesh);
		}

		void perpare_vertices()
		{
			int s1 = start_n;
			int c = 0;
			float r = start_r;
			while (c < count)
			{
				circle(r, s1); r += zl; s1 += 9;
				++c;
			}
		}

		void circle(float r,int n)
		{
			auto v = gen::circle(0.f, r, n);
			region.push_back(std::make_pair(vertices.size(), vertices.size() + v.size()));
			for (auto& vv : v)
				vertices.push_back({vv,wws::make_rgb(PREPARE_STRING("D936C0")).make<glm::vec3>() });
		}

		void on_update(float* data, int len)
		{

		}

		int fft_data_length()
		{
			return BASS_DATA_FFT256;
		}

		float start_r = 0.1f, zl = 0.1f;
		int start_n = 9;
		int count = 128;
		std::vector<Vertex> vertices;
		std::vector<std::pair<int, int>> region;
	};
}