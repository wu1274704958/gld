#pragma once

#include <node/clip_node.h>
#include <component.h>
#include <comps/sundry.h>
#include <data_mgr.hpp>
#include <node/node_with_event.h>

namespace gld {

	

	struct Clip : public ClipNode<Component>, public evt::EventHandler<Node<Component>> {

		Clip(float w, float h,float originX = 0.f,float originY = 0.f) : width(w * Word::WORD_SCALE),height(h * Word::WORD_SCALE),originX(originX),originY(originY)
		{
			
		}

		virtual void create()
		{
			auto render = std::shared_ptr<Render>( new Render("base/empty_vs.glsl", "base/empty_fg.glsl") );
			auto program = DefDataMgr::instance()->load<DataType::Program>("base/empty_vs.glsl", "base/empty_fg.glsl");

			if (program->uniform_id("perspective") == -1)
			{
				program->locat_uniforms("perspective", "world", "model", "local_mat");
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

			auto coll = std::shared_ptr<gld::def::Collision>(new gld::def::Collision(
				vertices_, 0, indices_
			));

			add_comp<gld::def::Collision>(coll);

			add_comp<gld::Transform>(std::make_shared<gld::Transform>());
			local = std::make_shared<gld::LocalTransForm>();
			add_comp<gld::LocalTransForm>(local);
			node = std::make_shared< NodeWithEvent>();
			node->add_comp<gld::Transform>(std::make_shared<gld::Transform>());

			//node->add_comp<gld::def::Collision>(coll);
			
			add_child(node);
		}									   

		void set_size(float w, float h)
		{
			width = w * Word::WORD_SCALE;
			height = h * Word::WORD_SCALE;
		}

		void set_size_no_scale(float w, float h)
		{
			width = w;
			height = h;
		}

		void refresh()
		{
			local->scale = glm::vec3(width , height , 1.0f);
			get_comp<gld::def::Collision>()->matrix = glm::scale(glm::mat4(1.f), local->scale);
		}

		int evt_children_count() override
		{
			return static_cast<int>(children.size());
		}

		evt::EventHandler<Node<Component>>* evt_child(int i) override
		{
			try {
				return dynamic_cast<evt::EventHandler<Node<Component>>*>(children[i].get());
			}
			catch (std::bad_cast& e) { return nullptr; }
			return nullptr;
		}

		bool onHandleMouseEvent(evt::MouseEvent<Node<Component>>* e) override
		{
			auto coll = this->get_comp<gld::def::Collision>();
			if (!coll) return false;
			glm::vec2 braypos; float distance; glm::vec3 pos, model_pos;
			if (coll->ray_test(e->world, e->camera_pos, e->raydir, braypos, distance, pos, model_pos))
			{
				e->model_pos = model_pos;
				e->pos = pos;
				return true;
			}
			return false;
		}

		std::weak_ptr<Node<Component>> get_target() override
		{
			auto ptr = dynamic_cast<Node<Component>*>(this);
			return ptr->weak_from_this();
		}


		std::shared_ptr< NodeWithEvent> node;

		float get_width()
		{
			return width;
		}
		float get_height()
		{
			return height;
		}

	protected:
		float width, height,originX,originY;
		std::shared_ptr< LocalTransForm> local;
	};
}