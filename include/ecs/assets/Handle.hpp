#pragma once

// Compatibility names for the new intrusive asset handles.

#include "Assets.hpp"

namespace gld::ecs {

    template<class T>
    using Handle = AssetHandle<T>;
}
