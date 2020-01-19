#pragma once

#include <gl_comm.hpp>
#include <program.hpp>
#include <vertex_arr.hpp>
#include <drawable.h>
#include <vector>
#include "comm.h"

class View1 : public gld::Drawable
{
public:
	View1(gld::Program& p,gld::VertexArr& va,std::vector<Vertex> vertices,glm::vec4 color) : 
		Drawable(p,"model"),
		va(va),
		vertices(std::move(vertices))
	{
		for (auto& c : this->vertices)
		{
			c.color = color;
		}
		b = 0;
		count = static_cast<uint32_t>(this->vertices.size());
	}

	virtual ~View1()
	{

	}

	void onPreDraw()override 
	{
		glUniform1f(program.uniform_id("alpha"), alpha);
		glUniform1f(program.uniform_id("offsetZ"), offsetZ);
	}
	void onDraw()override 
	{
		va.bind();
		glDrawArrays(GL_TRIANGLE_STRIP, b, count);
		va.unbind();
	}
	void update()override 
	{
		
	}
	void onInit()override 
	{
		va.bind();

		va.buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(vertices, GL_STATIC_DRAW);

		va.buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
			gld::VAP_DATA<3, float, false>,
			gld::VAP_DATA<4, float, false>>();

		va.unbind();
	}
	float alpha = 1.0f, offsetZ = 0.0f;
protected:
	gld::VertexArr& va;
	std::vector<Vertex> vertices;
	uint32_t b = 0, count = 0;
	
};

