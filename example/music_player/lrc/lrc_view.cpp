#include "lrc_view.h"
#include <make_color.hpp>
using namespace gld;

namespace lrc{

    void LrcView::create()
    {
        add_comp(std::make_shared<Transform>());

        labs.push_back(std::shared_ptr<Label>(new Label()));
        labs[0]->font = font;
        labs[0]->create();
        labs[0]->color = wws::make_rgba(PREPARE_STRING("009E8EFF")).make<glm::vec4>();
        labs[0]->size = 120;
        labs[0]->onTextSizeChange = [this](float w,float h)
        {
            labs[0]->get_comp<Transform>()->pos = glm::vec3(-w/2.f,-h/2.f,0.f);
        };
        add_child(labs[0]);
    }
    void LrcView::on_play(std::shared_ptr<LrcView::LrcType> lrc)
    {
        curr = std::move(lrc);
    }
    void LrcView::playing(size_t idx)
    {
        if(curr)
        {
            labs[0]->set_text(curr->data[idx].line);
        }
    }

}