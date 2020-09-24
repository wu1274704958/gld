#include "lrc_view.h"
#include <make_color.hpp>
#include <serialization.hpp>
using namespace gld;

namespace lrc{

    LrcView::LrcView() : 
            gld::Clip(4000.f,284.f,0.5f,0.0f),
            wheel(3,0.236f,6)
            {}

    void LrcView::create()
    {
        stencil_val = 0x2;
        //debug_clip = true;
        Clip::create();

        create_labs();
        wheel.rotate = -90.f;
        wheel.unless = 10;
        wheel.set_curr(0);
        wheel.on_update_pos = [this](size_t ei,size_t i,glm::vec3 pos,bool is_none)
        {
            labs[ei]->get_comp<Transform>()->pos = pos;
            if(is_none)
                labs[ei]->set_text("");
            else{
                if(curr)
                    labs[ei]->set_text(curr->data[i].line);
            }
        };
        refresh();
        wheel.create();
    }
    void LrcView::create_labs()
    {
        for(int i = 0;i < 4;++i)
        {
            labs.push_back(std::shared_ptr<Label>(new Label()));
            labs[i]->font = font;
            labs[i]->create();
            labs[i]->color = wws::make_rgba(PREPARE_STRING("009E8EFF")).make<glm::vec4>();
            labs[i]->size = 42;
            labs[i]->onTextSizeChange = [this,i](float w,float h)
            {
                labs[i]->get_comp<Transform>()->pos += glm::vec3(-w/2.f,-h/2.f,0.f);
            };
            node->add_child(labs[i]);
        }
    }
    void LrcView::on_play(std::shared_ptr<LrcView::LrcType> lrc)
    {
        curr = std::move(lrc);
        wheel.set_count(curr->data.size());
        wheel.tween_to(0,17.f,tween::linear);
    }
    void LrcView::playing(size_t idx)
    {
        if(curr)
        {
            wheel.tween_to(idx,300.f);
        }
    }
    
    void LrcView::onUpdate()
    {
        
    }

}