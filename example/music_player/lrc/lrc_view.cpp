#include "lrc_view.h"
#include <make_color.hpp>
#include <serialization.hpp>
using namespace gld;

namespace lrc{

    void LrcView::create()
    {
        add_comp(std::make_shared<Transform>());
        create_labs();
        wheel.rotate = -90.f;
        wheel.unless = 10;
        wheel.on_update_pos = [this](size_t ei,size_t i,glm::vec3 pos,bool is_none)
        {
            labs[ei]->get_comp<Transform>()->pos = pos;
            if(is_none)
                labs[ei]->set_text("---------");
            else
                labs[ei]->set_text(wws::to_string(i));
        };
        wheel.create();
    }
    void LrcView::create_labs()
    {
        for(int i = 0;i < 6;++i)
        {
            labs.push_back(std::shared_ptr<Label>(new Label()));
            labs[i]->font = font;
            labs[i]->create();
            labs[i]->color = wws::make_rgba(PREPARE_STRING("009E8EFF")).make<glm::vec4>();
            labs[i]->size = 120;
            labs[i]->onTextSizeChange = [this,i](float w,float h)
            {
                labs[i]->get_comp<Transform>()->pos += glm::vec3(-w/2.f,-h/2.f,0.f);
            };
            add_child(labs[i]);
        }
    }
    void LrcView::on_play(std::shared_ptr<LrcView::LrcType> lrc)
    {
        curr = std::move(lrc);
    }
    void LrcView::playing(size_t idx)
    {
        if(curr)
        {
            //labs[0]->set_text(curr->data[idx].line);
        }
    }
    int a = 0;
    bool f = true; 
    void LrcView::onUpdate()
    {
        if(a >= 1000)
        {
            if(!f && wheel.get_curr() == 1) f = true;
            if(f && wheel.get_curr() == 20) f = false;

            int v = wheel.get_curr() + (f ? 1 : -1);
            wheel.tween_to(v,300);
            a = 0;

    
        }
        a += 16; 
    }

}