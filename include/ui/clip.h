#pragma once

#include <node/clip_node.h>
#include <component.h>
#include <comps/sundry.h>
#include <data_mgr.hpp>
#include <node/node_with_event.h>

namespace gld {

	

	struct Clip : public ClipNode<Component> {

		Clip(float w, float h,float originX = 0.f,float originY = 0.f) : width(w),height(h),originX(originX),originY(originY)
		{
			init();
			refresh();
		}

		void init()
		{
			auto render = std::shared_ptr<Render>( new Render("base/empty_vs.glsl", "base/empty_fg.glsl") );
			if (render->get()->uniform_id("perspective") == -1)
			{
				render->get()->locat_uniforms("perspective", "world", "model", "local_mat");
			}								   
			this->add_comp(render);
			
			std::shared_ptr<std::vector<float>> vertices_ = DefDataMgr::instance()->load<DataType::SquareVertices>(originX, originY);
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

			add_comp<gld::def::Mesh>(std::shared_ptr<gld::def::Mesh>(new gld::def::Mesh(
				indices_->size(), vertices_->size() / 3, std::move(vao)
			)));

			add_comp<gld::Transform>(std::make_shared<gld::Transform>());
			local = std::make_shared<gld::LocalTransForm>();
			add_comp<gld::LocalTransForm>(local);
			node = std::make_shared< NodeWithEvent>();
			node->add_comp<gld::Transform>(std::make_shared<gld::Transform>());
		}									   

		void set_size(float w, float h)
		{
			width = w;
			height = h;
		}

		void refresh()
		{
			local->scale = glm::vec3(width * Word::WORD_SCALE, height * Word::WORD_SCALE, 1.0f);
		}
	protected:
		float width, height,originX,originY;
		std::shared_ptr< LocalTransForm> local;
		std::shared_ptr< NodeWithEvent> node;
	};
}