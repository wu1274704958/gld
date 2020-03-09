#pragma once

#include <gl_comm.hpp>

namespace gld::buf{

    template <BufferType BT>
    class Buffer{
    public:
        Buffer()
        {
           
        }

        virtual void create()
        {
            glGenBuffers(1,&id);
        }

        virtual void destroy()
        {
            glDeleteBuffers(1,&id);
        }

        void clean()
        {
            if(good())
                destroy();
        }

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        Buffer(Buffer&& oth)
        {
            id = oth.id;
            oth.id = 0;
        }
        Buffer& operator=(Buffer&& oth)
        {
            clean();
            id = oth.id;
            oth.id = 0;
            return *this;
        }
        
        virtual ~Buffer()
        {
            clean();
        }

        virtual void bind()
        {
            glBindBuffer(static_cast<size_t>(BT),id);
        }

        virtual void unbind()
        {
            glBindBuffer(static_cast<size_t>(BT),0);
        }

        bool good()
        {
            return id > 0;
        }

        Glid get_id()
        {
            return id;
        }

    protected:
        Glid id = 0;
    };

}