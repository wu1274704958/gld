#pragma once

#include <type_traits>
#include <vertex_arr.hpp>

namespace gld{


    template<ArrayBufferType ABT,unsigned int Binding,typename T>
    class Buffer : public VABuffer<ABT>
    {
        using BaseTy = VABuffer<ABT>;
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

        T* operator->() 
        {
            return &data;
        }

        operator const T& () const
        {
            return data;
        }

        const T& operator*() const 
        {
            return data;
        }

        const T* operator->() const
        {
            return &data;
        }

        void bind_buf_base()
        {
            glBindBufferBase(static_cast<unsigned int>(ABT),static_cast<unsigned int>(Binding), BaseTy::id);
        }

        T* map(int64_t offset,unsigned int map_flag)
        {
            return reinterpret_cast<T*>(glMapBufferRange(
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
        using BaseTy = Buffer<ArrayBufferType::UNIFORM,Binding,T>;
    public:
        UniformBuf(){}
        template<typename ...Args>
        UniformBuf(Args&&...args) : Buffer<ArrayBufferType::UNIFORM,Binding,T>(std::forward<Args>(args)...){}


        void init(GLenum ty)
        {
            BaseTy::create();
            BaseTy::bind();
            BaseTy::bind_null_data(ty);
            BaseTy::unbind();
        }

        virtual void sync(unsigned int map_flag){
            BaseTy::bind_buf_base();

            T* map_block =  BaseTy::map(0,map_flag);
            if(map_block)
            {
                std::memmove(map_block,&(BaseTy::data),sizeof(T));
            }
             BaseTy::unmap();
        }
    };
}