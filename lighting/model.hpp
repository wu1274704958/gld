#pragma once

#include <gl_comm.hpp>
#include <program.hpp>
#include <vertex_arr.hpp>
#include <drawable.h>
#include <vector>

class View1 : public gld::Drawable
{
public:
	View1(gld::Program& p,gld::VertexArr& va,glm::vec3 color,uint32_t triangle_count) : 
		Drawable(p,"model"),
		va(va),
        color(color),
        draw_count(triangle_count * 3)
	{
		k_color = p.uniform_id("obj_color");
	}

	virtual ~View1()
	{

	}

	void onPreDraw()override 
	{
		glUniform3fv(k_color,3,glm::value_ptr(color));
	}
	void onDraw()override 
	{
		va.bind();
		glDrawArrays(GL_TRIANGLES, 0, draw_count * 3);
		va.unbind();
	}
	void update()override 
	{
		
	}
	void onInit()override 
	{
		
	}
protected:
	gld::VertexArr& va;
    glm::vec3 color;
    int k_color;
    uint32_t draw_count;
};

