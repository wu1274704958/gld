#pragma once

#include <vertex_arr.hpp>

namespace gld{


    template<ArrayBufferType ABT,unsigned int Binding,typename T>
    class Buffer : public VABuffer<ABT>
    {
        static_assert(std::is_standard_layout_v<T>,"this type must is standard!!!");
    public:
        Buffer(){}
        template<typename ...Args>
        Buffer(Args&&...args) : data(std::forward<Args>(args)...){}

        void bind_null_data(GLenum ty)
        {
            glBufferData(static_cast<unsigned int>(ABT), sizeof(T), nullptr, ty);
        }

        T operator=(T d)
        {
            data = d;
            return d;
        }

        operator T& ()
        {
            return data;
        }

        T& operator*()
        {
            return data;
        }

        void bind_buf_base()
        {
            glBindBufferBase(static_cast<unsigned int>(ABT),static_cast<unsigned int>(Binding),id);
        }

        T* map(int64 offset,unsigned int map_flag)
        {
            return dynamic_cast<T*>(glMapBufferRange(
                            static_cast<unsigned int>(ABT),
                            offset,
                            sizeof(T),
                            map_flag)
            );
        }

        void unmap()
        {
            glUnmapBuffer(static_cast<unsigned int>(ABT));
        }

    protected:
        T data;
    };

    template<unsigned int Binding,typename T>
    class UniformBuf : public Buffer<ArrayBufferType::UNIFORM,Binding,T>
    {
    public:
        UniformBuf(){}
        template<typename ...Args>
        UniformBuf(Args&&...args) : Buffer<ArrayBufferType::UNIFORM,Binding,T>(std::forward<Args>(args)...){}


        void init(GLenum ty)
        {
            create();
            bind();
            bind_null_data(ty);
            unbind();
        }

        virtual void sync(unsigned int map_flag){
            bind_buf_base();

            T* map_block = map(0,map_flag);
            if(map_block)
            {
                std::memmove(map_block,&data,sizeof(T));
            }
            unmap();
        }
    };
}