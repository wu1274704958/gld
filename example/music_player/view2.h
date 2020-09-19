#pragma once

#include <generator/Generator.hpp>
#include "fft_view.h"

namespace gld {
	struct View2 : public FFTView {
		struct Vertex
		{
			glm::vec3 pos;
			glm::vec3 color;
		};

		View2() : line_width("line_width") {}

		void create()
		{
			perpare_vertices();
			add_comp(std::make_shared<Transform>());

			auto render = std::shared_ptr<Render>(new Render("base/line3_vs.glsl", "base/line3_fg.glsl", "base/line3_ge.glsl"));
			render->init();
			auto program = render->get();
			if (program->uniform_id("perspective") == -1)
				program->locat_uniforms("perspective", "world","model","line_width");
			add_comp<Render>(render);

			line_width.attach_program(program);

			program->use();
			line_width = 0.01f;

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
			mesh->mode = GL_LINE_LOOP;
			//mesh->mode = GL_LINES;

			add_comp<def::Mesh>(mesh);
		}

		void perpare_vertices()
		{
			auto v = gen::circle(0.f, radius, count);
			for (auto& vv : v)
				vertices.push_back({ vv,wws::make_rgb(PREPARE_STRING("D936C0")).make<glm::vec3>() });
		}

		void on_update(float* data, int len)
		{
			constexpr int c = 128;
			int unit_c = count / c;


			float b = 0;//sqrtf(data[len - 1]) * 2.f;
			for (int i = 0; i < c; ++i)
			{
				float m = sqrtf(data[i]) * 1.f;
				//float e = sqrtf(data[((i == len - 1) ? len - 1 : i + 1)]) * 2.f;
				set_part(i, unit_c, 0, unit_c, b, m);
				//set_part(i, unit_c, unit_c / 2, unit_c, m, e);
				b = m;
			}
			auto& vao = get_comp< def::Mesh>()->vao;
			vao->bind();
			vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(vertices, GL_STATIC_DRAW);
			vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
				gld::VAP_DATA<3, float, false>, gld::VAP_DATA<3, float, false>>();
			vao->unbind();
		}

		void set_part(int i,int unit_c,int m, int n, float b, float e)
		{
			auto  f = b < e ? tween::linear: tween::linear;
			for (int j = m; j < n; ++j)
			{
				float v = f((float)(j - m) / (float)(n - m), 0.f, 1.0f, 1.f) * (e - b) + b;
				set_unit(i * unit_c + j, v);
			}
		}

		void set_unit(int i,float v)
		{
			{
				vertices[i].pos.y = v;
				vertices[i].color.x = v;
			}
		}

		size_t fft_data_length()
		{
			return BASS_DATA_FFT256;
		}

		float radius = 1.f;
		int count = 4096;
		std::vector<Vertex> vertices;

		Uniform<UT::Float> line_width;
	};
}