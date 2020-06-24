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
			std::vector <std::tuple<float, float,int>> v;

			for(int i = 0;i < level / 2;++i)
			{ 
				float a =  0.5f - ((float)i / (float)level);
				float b = glm::sqrt( glm::pow(0.5f, 2.0f) - glm::pow(a, 2.f) );
				int seg = (int)(b * (float)equator_count);

				v.push_back(std::make_tuple(a,b, seg));

				auto res = gen::circle(a, b, seg);

				for (auto& r : res)
					standBy.push_back(r);

			}
			
			if (level % 2 != 0)
			{
				auto res = gen::circle(0.f, 0.5f, equator_count);

				for (auto& r : res)
					standBy.push_back(r);
			}

			for (int i = 0; i < v.size(); ++i)
			{
				auto [a, b, seg] = v[i];
				auto res = gen::circle(-a, b, seg);

				for (auto& r : res)
					standBy.push_back(r);

			}
				
		}

		
		int equator_count = 24;
		int level = 13;

		//std::unordered_map < glm::vec3, std::shared_ptr<Node<Component>> > pos_map;
		std::vector<glm::vec3> frontline;
		std::vector<glm::vec3> standBy;

	};
}