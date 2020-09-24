#pragma once
#include <node.hpp>
#include <component.h>
#include "comm.h"
#include <ui/label.h>
#include "ui/wheel_ex.h"
namespace lrc{
    
    struct LrcView : public gld::Node<gld::Component>
    {
        using LrcType = Lrc;
        void create();
        void on_play(std::shared_ptr<LrcType> lrc);
        void playing(size_t idx);
        LrcView() : 
            gld::Node<gld::Component>(),
            wheel(3,0.236f,6)
            {}
        void onUpdate() override;
    protected:
        void create_labs();
        std::shared_ptr<LrcType> curr;
        std::vector< std::shared_ptr<gld::Label> > labs;
        const char* font = "fonts/happy.ttf"; 
        gld::WheelEx wheel;
    };
    
}