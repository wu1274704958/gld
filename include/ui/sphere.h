#pragma once

#include "word.h"
#include <node/node_with_event.h>
#include <unordered_map>
#include <vector>

namespace gld {
	struct Sphere : public NodeWithEvent {

		Sphere() {}
		Sphere(int equator_count, int level) : equator_count(equator_count), level(level)
		{
			
		}

		void create()
		{
			add_comp(std::shared_ptr<Transform>(new Transform()));
			float m = glm::pi<float>() / 2.f;
			std::vector <std::pair<float, int>> v;

			for(int i = 0;i < level / 2;++i)
			{ 
				float a =  0.5f - ((float)i / (float)level);
				float b = glm::sqrt( glm::pow(0.5f, 2.0f) - glm::pow(a, 2.f) );
				//int seg = ;
			}
			
		}

		
		int equator_count = 12;
		int level = 5;

		std::unordered_map < glm::vec3, std::shared_ptr<Node<Component>> > pos_map;
		std::vector<glm::vec3> frontline;
		std::vector<glm::vec3> standBy;

	};
}