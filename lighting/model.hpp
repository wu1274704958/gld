#pragma once

#include <gl_comm.hpp>
#include <program.hpp>
#include <vertex_arr.hpp>
#include <drawable.h>
#include <vector>
#include <uniform.hpp>
#include "light.hpp"

namespace gld{
	
class Model : public gld::Drawable
{
public:
	Model(gld::Program& p,gld::VertexArr& va,glm::vec3 color,uint32_t triangle_count) : 
		Drawable(p,"model"),
		va(va),
		material(p),
		color(color),
        draw_count(triangle_count * 3)
	{
		
	}

	virtual ~Model()
	{

	}

	void onPreDraw()override 
	{
		material.color = glm::value_ptr(color);
		material.ambient_strength = ambient_strength;
		material.shininess = shininess;
		material.specular_strength = specular_strength;
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
	glm::vec3 color;
    float ambient_strength;
    float specular_strength;
    float shininess;
protected:
	gld::VertexArr& va;
    uint32_t draw_count;
};

}
