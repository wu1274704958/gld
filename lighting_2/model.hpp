#pragma once

#include <gl_comm.hpp>
#include <program.hpp>
#include <vertex_arr.hpp>
#include <drawable.h>
#include <vector>
#include <uniform.hpp>
#include "light.hpp"
#include <glm_uniform.hpp>

namespace gld{
	
class Model : public gld::Drawable
{
public:
	Model(gld::Program& p,gld::VertexArr& va,uint32_t triangle_count) : 
		Drawable(p,"model"),
		material(p),
		va(va),
        draw_count(triangle_count * 3)
	{

	}

	virtual ~Model()
	{

	}

	void onPreDraw()override 
	{
		material.diffuseTex.sync();
		material.ambient_strength.sync();
		material.shininess.sync();
		material.specular_strength.sync();
	}
	void onDraw()override 
	{
		va.bind();
		glDrawArrays(GL_TRIANGLES, 0, draw_count);
		va.unbind();
	}
	void update()override 
	{
		
	}
	void onInit()override 
	{
		
	}
	Material material;
protected:
	gld::VertexArr& va;
    uint32_t draw_count;
};

}
