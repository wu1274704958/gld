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
			mesh->mode = GL_LINE_LOOP;
			//mesh->mode = GL_POINTS;

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

			
			for (int i = 0; i < len; ++i)
				data[i] = sundry::rd_0_1() * 0.2f;

			data[0] = 0.f;
			data[1] = 0.1f;
			data[2] = 0.12f;
			data[3] = 0.13f;
			data[4] = 0.08f;
			data[5] = 0.17f;

			float b = sqrtf(data[len - 1]) * 2.f;
			for (int i = 0; i < c; ++i)
			{
				float m = sqrtf(data[i]) * 2.f;
				float e = sqrtf(data[((i == len - 1) ? 0 : i + 1)]) * 2.f;
				e -= m * 0.24f;
				set_part(i, unit_c, 0, unit_c / 2, b, m);
				set_part(i, unit_c, unit_c / 2, unit_c, m, e);
				b = e;
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
			auto  f = b < e ? tween::Expo::easeOut : tween::Expo::easeIn;
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
	};
}