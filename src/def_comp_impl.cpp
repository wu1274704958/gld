#include <comps/Material.hpp>
#include <memory>

namespace gld::def{

    std::shared_ptr<MeshInstanced> MeshInstanced::create_with_mesh(Mesh* m,size_t count)
    {
        if(m)
        {
            return std::shared_ptr<MeshInstanced>(new MeshInstanced(m->vertex_size,m->index_size,count,m->vao));
        }else
            return nullptr;
    }

}

