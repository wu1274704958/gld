#include <glsl_preprocess.hpp>

namespace gld::glsl{

    std::shared_ptr<IncludeCatche> IncludeCatche::self = std::shared_ptr<IncludeCatche>();
    std::shared_ptr<IncludeCatche> IncludeCatche::get_instance()
    {
        if(!self)
        {
            self = std::make_shared<IncludeCatche>();
        }
        return self;
    }

}