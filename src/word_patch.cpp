#include <ui/word_patch.h>
#include <node/clip_node.h>
#include <vertex_arr.hpp>
#include <comps/Material.hpp>
#include <text/TextNodeGen.hpp>

namespace gld {


	struct PatchTextMeshInstanced : public Component
	{
		PatchTextMeshInstanced(
			size_t index_size,
			size_t vertex_size,
			size_t count,
			std::shared_ptr<gld::VertexArr> vao,
			std::unordered_map<std::shared_ptr<Texture<TexType::D2>>, std::vector<WordPatch::WordData>>& word_data)
			: index_size(index_size),
			vertex_size(vertex_size),
			instance_count(count),
			vao(std::move(vao)),
			mode(GL_TRIANGLES),
			word_data(word_data)
		{
			
		}

		void draw() override
		{
			if (!word_data.empty())
			{
				auto mater = get_node()->get_comp<txt::PatchTextMaterial>();
				for (auto& wd : word_data)
				{
					mater->diffuseTex = wd.first;
					mater->draw();

					instance_count = wd.second.size();
					vao->bind_self();
					auto& buf = vao->get_oths()[0];
					buf.bind();
					buf.bind_data(wd.second, GL_STATIC_DRAW);
					buf.vertex_attrib_pointer<1, 
						VAP_DATA<4, float, false>,//uv1
						VAP_DATA<4, float, false>,//uv2
						VAP_DATA<4, float, false>,//color 
						VAP_DATA<4, float, false>,//model
						VAP_DATA<4, float, false>, 
						VAP_DATA<4, float, false>, 
						VAP_DATA<4, float, false>, 
						VAP_DATA<4, float, false>,//local
						VAP_DATA<4, float, false>,
						VAP_DATA<4, float, false>,
						VAP_DATA<4, float, false>
					>();
					vao->vertex_attr_div<1, 
						1, 1, 1,    //uv1 2 color
						1, 1, 1, 1, // model
						1, 1, 1, 1  // local
					>();
					vao->unbind_self();

					draw_();
				}
			}
		}

		void draw_()
		{
			vao->bind();
			if (vao->buffs().get<ArrayBufferType::ELEMENT>().good())
			{
				glDrawElementsInstanced(mode, static_cast<GLsizei>(index_size), MapGlTypeEnum<unsigned int>::val, nullptr, static_cast<GLsizei>(instance_count));
			}
			else {
				glDrawArraysInstanced(mode, 0, static_cast<GLsizei>(vertex_size), static_cast<GLsizei>(instance_count));
			}
			vao->unbind();
		}

		int64_t idx() override { return 100; }
		size_t index_size;
		size_t vertex_size;
		size_t instance_count;
		std::shared_ptr<gld::VertexArr> vao;
		GLenum mode;
		std::unordered_map<std::shared_ptr<Texture<TexType::D2>>, std::vector<WordPatch::WordData>>& word_data;
	};
	

	WordPatch::WordPatch()
	{
		
	}

	void WordPatch::create()
	{
		init_word();
	}

	void WordPatch::init_word()
	{
	
		auto render = std::shared_ptr<Render>(new Render("instantiation/word_vs.glsl", "instantiation/word_fg.glsl"));
		render->init();
		auto program = render->get();
		if (program->uniform_id("perspective") == -1)
			program->locat_uniforms("perspective", "world","diffuseTex","blurEdgeN");
		add_comp<Render>(render);

		std::shared_ptr<std::vector<float>> vertices_ = DefDataMgr::instance()->load<DataType::SquareVertices>(0.f, 0.f);
		std::shared_ptr<std::vector<int>> indices_ = DefDataMgr::instance()->load<DataType::SquareIndices>();

		auto vao = std::make_shared<gld::VertexArr>();
		vao->create();
		vao->create_arr<gld::ArrayBufferType::VERTEX>();
		vao->create_arr<gld::ArrayBufferType::ELEMENT>();
		vao->bind();
		vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(*vertices_, GL_STATIC_DRAW);
		vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
			gld::VAP_DATA<3, float, false>>();
		vao->buffs().get<gld::ArrayBufferType::ELEMENT>().bind_data(*indices_, GL_STATIC_DRAW);
		vao->unbind();
		vao->create_one();

		add_comp<PatchTextMeshInstanced>(std::shared_ptr<PatchTextMeshInstanced>(new PatchTextMeshInstanced(
			indices_->size(), vertices_->size() / 3, 1, std::move(vao),word_data
		)));
		add_comp<txt::PatchTextMaterial>(std::make_shared<txt::PatchTextMaterial>());
		
	}

	
	size_t WordPatch::submit_word_(std::shared_ptr<Texture<TexType::D2>>& tex, WordData& res)
	{
		if (!has(tex))
		{
			word_data[tex] = std::vector<WordData>();
			word_data[tex].reserve(32);
		}
		word_data[tex].push_back(res);
		return word_data[tex].size() - 1;
	}
	void WordPatch::submit_word_(std::shared_ptr<Texture<TexType::D2>>& tex, size_t idx, WordData& res)
	{
		if (has(tex) && idx < word_data[tex].size())
		{
			word_data[tex][idx] = res;
		}
	}

	bool WordPatch::has(std::shared_ptr<Texture<TexType::D2>>& tex)
	{
		return word_data.find(tex) != word_data.end();
	}

	void WordPatch::clear()
	{
		word_data.clear();
		ws.clear();
		//ws_map.clear();
	}

	void WordPatch::add_word(std::shared_ptr<Word> w,glm::vec2 origin)
	{
		ws.push_back(std::make_pair(w,origin));
		//WordData wd;
		//create_word_data(ws.back(), wd);
		//ws_map[ws.size() - 1] = submit_word_(w->get_comp<txt::DefTextMaterial>()->diffuseTex, wd);
	}

	void WordPatch::create_word_data(std::pair<std::shared_ptr<Word>, glm::vec2>& pair, WordData& res)
	{
		glm::mat4 local(1.f);
		local = glm::translate(local, glm::vec3(pair.second.x, pair.second.y, 0.f));
		auto trans = pair.first->get_comp<Transform>();
		auto parent = get_parent();
		auto model = trans->get_model();
		auto mater = pair.first->get_comp<txt::DefTextMaterial>();
		if (parent)
			model = parent->get_comp<Transform>()->get_model() * model;
		//glm::vec2 uv[] = {
		//	glm::vec2(pair.first->rect.x + pair.first->rect.z, pair.first->rect.y),
		//	glm::vec2(pair.first->rect.x,  pair.first->rect.y),
		//	glm::vec2(pair.first->rect.x,  pair.first->rect.y + pair.first->rect.w),
		//	glm::vec2(pair.first->rect.x + pair.first->rect.z, pair.first->rect.y + pair.first->rect.w)
		//};
		res.color = mater->color;
		res.local = local;
		res.model = model;
		res.uv = glm::vec4(pair.first->rect.x + pair.first->rect.z, pair.first->rect.y, pair.first->rect.x, pair.first->rect.y);
		res.uv2 = glm::vec4(pair.first->rect.x, pair.first->rect.y + pair.first->rect.w, pair.first->rect.x + pair.first->rect.z, pair.first->rect.y + pair.first->rect.w);
	}

	bool WordPatch::refresh(size_t i)
	{
		if (i < ws.size())
		{
			WordData wd;
			create_word_data(ws[i], wd);
			//submit_word_(ws[i].first->get_comp<txt::DefTextMaterial>()->diffuseTex,ws_map[i], wd);
			return true;
		}
		return false;
	}
	void WordPatch::refresh()
	{
		word_data.clear();
		//ws_map.clear();
		//int i = 0;
		for (auto& w : ws)
		{
			WordData wd;
			create_word_data(w, wd);
			submit_word_(w.first->get_comp<txt::DefTextMaterial>()->diffuseTex, wd);
			//ws_map[i++] = submit_word_(w.first->get_comp<txt::DefTextMaterial>()->diffuseTex, wd);
		}
	}
	size_t WordPatch::word_count()
	{
		return ws.size();
	}

	std::shared_ptr<Word> WordPatch::get_word(size_t idx)
	{
		return ws[idx].first;
	}

	void WordPatch::onDraw()
	{
		refresh();
	}

}