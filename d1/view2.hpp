#pragma once

#include "view1.hpp"

class View2 : public View1
{
public:
	View2(gld::Program& p, gld::VertexArr& va, std::vector<Vertex> vertices, glm::vec4 color) :
		View1(p,va,std::move(vertices), color)
	{
		vertex_size = static_cast<int>( this->vertices.size());
        count = min_count;
	}

	void onPreDraw()override
	{
		View1::onPreDraw();
        update_color();
        update_vertices();
	}
	
	void update()override
	{
        ++draw_idx;
        if (draw_idx == draw_dur)
        {
            draw_idx = 0;

            b += 2;

            if (b + min_count >= vertex_size)
            {
                b = 0;
                count = min_count;
              
            }
            else
            if (b + count >= vertex_size)
                count = vertex_size - b;
            else {
                if (count < origin_count)
                    count += 2;
            }
        }
	}

    virtual void update_color()
    {
        float of = 1.0f - sundry::normal_dist(0.f);
        int m = count / 2;
        for (int i = 0; i < count; ++i)
        {
            float ni = static_cast<float>(i - m);
            float nd = sundry::normal_dist(ni / 2.6f);
            vertices[b + i].color.a = nd + of;
            vertices[b + i].color.r = nd;
        }
    }

    void update_vertices()
    {
        va.bind();
        va.buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(vertices, GL_STATIC_DRAW);

        va.buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
            gld::VAP_DATA<3, float, false>,
            gld::VAP_DATA<4, float, false>>();
        va.unbind();
    }


	int origin_count = 16, min_count = 4;
	int draw_dur = 1;
	int draw_idx = 0;

protected:
	int vertex_size;
	
};