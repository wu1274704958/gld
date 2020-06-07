#include <text/TextNodeGen.hpp>
#include <component.h>
#include <data_mgr.hpp>
 
using namespace gld;

std::shared_ptr<gld::Node<gld::Component>> txt::DefTextNodeGen::generate(std::shared_ptr<gld::Texture<gld::TexType::D2>> tex, WordData& wd)
{
    return std::shared_ptr<gld::Node<gld::Component>>();
}
