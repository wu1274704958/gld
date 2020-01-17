#pragma once

#include <gl_comm.hpp>
#include <vector>

namespace gld
{
    class VertexArr;

    template<size_t S,typename Ty,bool Normalized>
    struct VAP_DATA{
        constexpr static size_t len = S;
        using type = Ty;
        constexpr static bool normalized = Normalized;
    };

    template <ArrayBufferType ABT>
    class VABuffer{
    public:
        VABuffer()
        {
           
        }

        void create()
        {
            glGenBuffers(1,&id);
        }

        void clean()
        {
            if(good())
                glDeleteBuffers(1,&id);
        }

        VABuffer(const VABuffer&) = delete;
        VABuffer& operator=(const VABuffer&) = delete;

        VABuffer(VABuffer&& oth)
        {
            id = oth.id;
            oth.id = 0;
        }
        VABuffer& operator=(VABuffer&& oth)
        {
            clean();
            id = oth.id;
            oth.id = 0;
            return *this;
        }
        
        ~VABuffer()
        {
            clean();
        }

        void bind()
        {
            glBindBuffer(static_cast<size_t>(ABT),id);
        }

        void unbind()
        {
            glBindBuffer(static_cast<size_t>(ABT),0);
        }

        template<typename T>
        void bind_data(std::vector<T> v,GLenum ty)
        {
            glBufferData(static_cast<size_t>(ABT),sizeof(T) * v.size(),v.data(),ty);
        }

        template<typename T,size_t N>
        void bind_data(T (&v)[N],GLenum ty)
        {
            glBufferData(static_cast<size_t>(ABT),sizeof(T) * N,v,ty);
        }

        template<size_t Stride,typename ...Ts>
        void verte_attrib_pointer()
        {
            if constexpr(sizeof...(Ts) > 0)
            {
                verte_attrib_pointer_sub<Stride,0,0,Ts...>();
            }
        }

        template<size_t Stride,size_t Idx,size_t Off,typename T,typename ...Ts>
        void verte_attrib_pointer_sub()
        {
            static_assert( std::is_same_v<T,VAP_DATA> );
            glVertexAttribPointer(Idx,T::template len,T::template type,T::template normalized,Stride,(void *)Off);
            glEnableVertexAttribArray(Idx);
            if constexpr(sizeof...(Ts) > 0)
            {
                verte_attrib_pointer_sub<Stride,Idx + 1,sizeof(T::template type) * T::template len,Ts...>();
            }
        }

        bool good()
        {
            return id > 0;
        }
    private:
        Glid id = 0;
    };

    class VABuffers{
    public:
        using MapType = IdxArr<static_cast<size_t>(ArrayBufferType::VERTEX),static_cast<size_t>(ArrayBufferType::ELEMENT)>;

        template<ArrayBufferType ST>
        void set(VABuffer<ST> v){
            std::get<get_idx<static_cast<size_t>(ST)>(MapType())>(tup) = std::move(v);
        }

        template<ArrayBufferType ST>
        VABuffer<ST>& get(){
            return std::get<get_idx<static_cast<size_t>(ST)>(MapType())>(tup);
        }
		VABuffers()
        {

        }
		VABuffers(const VABuffers&) = delete;
		VABuffers(VABuffers&& s)
            
        {
			tup = std::move(tup);
		}

		VABuffers& operator=(const VABuffers&) = delete;
		VABuffers& operator=(VABuffers&& s) {
			tup = std::move(tup);
			return *this;
		}

        private:
        std::tuple<VABuffer<ArrayBufferType::VERTEX>,VABuffer<ArrayBufferType::ELEMENT>> tup;
    };

    class VertexArr{
    public:
        VertexArr()
        {
            
        }

        void create()
        {
            glGenVertexArrays(1,&id);
        }

        void clean()
        {
            if(good())
                glDeleteVertexArrays(1,&id);
        }

        VertexArr(const VertexArr&) = delete;
        VertexArr& operator=(const VertexArr&) = delete;

        VertexArr(VertexArr&& oth)
        {
            id = oth.id;
            oth.id = 0;
            vbuffs = std::move(vbuffs);
        }
        VertexArr& operator=(VertexArr&& oth)
        {
            clean();
            id = oth.id;
            oth.id = 0;
            vbuffs = std::move(vbuffs);
            return *this;
        }
        
        ~VertexArr()
        {
            clean();
        }

        void bind()
        {
            glBindVertexArray(id);
            if(vbuffs.get<ArrayBufferType::VERTEX>().good())
                vbuffs.get<ArrayBufferType::VERTEX>().bind();
            if(vbuffs.get<ArrayBufferType::ELEMENT>().good())
                vbuffs.get<ArrayBufferType::ELEMENT>().bind();
        }

        void unbind()
        {
            glBindVertexArray(0);
            vbuffs.get<ArrayBufferType::VERTEX>().unbind();
            vbuffs.get<ArrayBufferType::ELEMENT>().unbind();
        }

        bool good()
        {
            return id > 0;
        }

        template<ArrayBufferType ABT>
        void create_arr()
        {
            vbuffs.get<ABT>().create();
        }
        
        VABuffers& buffs()
        {
            return vbuffs;
        }
    private:
        Glid id = 0;  
        VABuffers vbuffs;
    };
} // namespace gld
