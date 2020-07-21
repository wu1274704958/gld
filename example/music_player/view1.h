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

		View1() : line_width("line_width")
		{

		}

		void create()
		{
			perpare_vertices();
			add_comp(std::make_shared<Transform>());

			auto render = std::shared_ptr<Render>(new Render("base/line2_vs.glsl", "base/line2_fg.glsl", "base/line2_ge.glsl"));
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
			mesh->mode = GL_LINES;
			add_comp<def::Mesh>(mesh);
		}

		void perpare_vertices()
		{
			int s1 = start_n;
			int c = 0;
			float r = start_r;
			while (c < count)
			{
				circle(r, s1); r += zl; s1 += count_zl;
				++c;
			}
		}

		void circle(float r,int n)
		{
			auto v = gen::circle(0.f, r, n);
			region.push_back(std::make_pair(vertices.size(), vertices.size() + v.size()));
			for (auto& vv : v)
				vertices.push_back({ glm::rotateY(vv, angle) ,wws::make_rgb(PREPARE_STRING("D936C0")).make<glm::vec3>() });
			angle += 0.01f;
		}

		void on_update(float* data, int len)
		{
			constexpr int c = 32;
			int unit_c = count / c;

			for (int i = 0; i < c; ++i)
			{
				float b = sqrtf(data[i]) * 1.f;
				float e = sqrtf(data[i + 1]) * 1.f;
				for (int j = 0; j < unit_c; ++j)
				{
					float v = b < e ? tween::Sine::easeOut((float)j / (float)unit_c, 0.f, 1.0f, 1.f) * (e - b) + b :
						tween::Sine::easeIn((float)j / (float)unit_c, 0.f, 1.0f, 1.f) * (e - b) + b;
					set_unit(i * unit_c + j, v);
				}
				
			}
			auto& vao = get_comp< def::Mesh>()->vao;
			vao->bind();
			vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(vertices, GL_STATIC_DRAW);
			vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
				gld::VAP_DATA<3, float, false>, gld::VAP_DATA<3, float, false>>();
			vao->unbind();
		}

		void set_unit(int i,float v)
		{
			for (int j = region[i].first; j < region[i].second; ++j)
			{
				vertices[j].pos.y = v;
				vertices[j].color.x = v;
			}
		}

		size_t fft_data_length()
		{
			return BASS_DATA_FFT256;
		}

		float start_r = 0.01f, zl = 0.2f, angle = 0.f;
		int start_n = 2;
		int count = 128,count_zl = 1;
		std::vector<Vertex> vertices;
		std::vector<std::pair<int, int>> region;
		Uniform<UT::Float> line_width;
	};
}