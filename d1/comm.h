#pragma once

struct Vertex {
	glm::vec3 pos;
	glm::vec4 color;
	Vertex(glm::vec3 pos) : pos(pos)
	{
		color = glm::vec4(0.f, 1.f, .0f, 0.f);
	}
	Vertex(glm::vec3 pos, glm::vec4 color) : pos(pos), color(color) {}
};