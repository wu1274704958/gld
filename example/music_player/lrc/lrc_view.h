#pragma once
#include <node.hpp>
#include <component.h>
#include "comm.h"
#include <ui/label.h>
namespace lrc{
    
    struct LrcView : public gld::Node<gld::Component>
    {
        using LrcType = Lrc;
        void create();
        void on_play(std::shared_ptr<LrcType> lrc);
        void playing(size_t idx);
    protected:
        std::shared_ptr<LrcType> curr;
        std::vector< std::shared_ptr<gld::Label> > labs;
        const char* font = "fonts/happy.ttf"; 
    };
    
}