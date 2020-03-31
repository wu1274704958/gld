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
        
        static constexpr size_t map_gl_type_enum()
        {
            return MapGlTypeEnum<Ty>::val;
        }
        
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
        void bind_data(std::vector<T>& v,GLenum ty)
        {
            glBufferData(static_cast<size_t>(ABT),sizeof(T) * v.size(),v.data(),ty);
        }

        template<typename T>
        void bind_data(std::vector<T>&& v, GLenum ty)
        {
            glBufferData(static_cast<size_t>(ABT), sizeof(T)* v.size(), v.data(), ty);
        }

        template<typename T,size_t N>
        void bind_data(T (&v)[N],GLenum ty)
        {
            glBufferData(static_cast<size_t>(ABT),sizeof(T) * N,v,ty);
        }

        template<typename T>
        void bind_data(T* data,size_t size,GLenum ty)
        {
            glBufferData(static_cast<size_t>(ABT),static_cast<long long int>(sizeof(T) * size),data,ty);
        }

        bool good()
        {
            return id > 0;
        }

        template<typename ...Ts>
        void vertex_attrib_pointer()
        {
            if constexpr(sizeof...(Ts) > 0)
            {
                constexpr size_t stride = vertex_attrib_stride<Ts...>();
                vertex_attrib_pointer_sub<stride,0,0,Ts...>();
            }
        }

        template<size_t B,typename ...Ts>
        void vertex_attrib_pointer()
        {
            if constexpr(sizeof...(Ts) > 0)
            {
                constexpr size_t stride = vertex_attrib_stride<Ts...>();
                vertex_attrib_pointer_sub<stride,B,0,Ts...>();
            }
        }

        template<size_t Stride,size_t Idx,size_t Off,typename T,typename ...Ts>
        void vertex_attrib_pointer_sub()
        {
            glEnableVertexAttribArray(Idx);
            glVertexAttribPointer(Idx,T::len,static_cast<int>( T::map_gl_type_enum() ),T::normalized,Stride,(void *)Off);

            if constexpr(sizeof...(Ts) > 0)
            {
                vertex_attrib_pointer_sub<Stride,Idx + 1,Off + sizeof(typename T::type) * T::len,Ts...>();
            }
        }

        template<typename T, typename ...Ts>
        static constexpr size_t vertex_attrib_stride()
        {
            constexpr size_t res = sizeof(typename T::type) * T::len;
            if constexpr (sizeof...(Ts) > 0)
            {
                return res + vertex_attrib_stride<Ts...>();
            }
            else {
                return res;
            }
        }

    protected:
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
			tup = std::move(s.tup);
		}

		VABuffers& operator=(const VABuffers&) = delete;
		VABuffers& operator=(VABuffers&& s) {
			tup = std::move(s.tup);
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
            vbuffs = std::move(oth.vbuffs);
            oths = std::move(oth.oths);
        }
        VertexArr& operator=(VertexArr&& oth)
        {
            clean();
            id = oth.id;
            oth.id = 0;
            vbuffs = std::move(oth.vbuffs);
            oths = std::move(oth.oths);
            return *this;
        }
        
        ~VertexArr()
        {
            clean();
        }

        void bind_self()
        {
            glBindVertexArray(id);
        }

        void unbind_self()
        {
            glBindVertexArray(0);
        }

        void bind()
        {
            bind_self();
            if(vbuffs.get<ArrayBufferType::VERTEX>().good())
                vbuffs.get<ArrayBufferType::VERTEX>().bind();
            if(vbuffs.get<ArrayBufferType::ELEMENT>().good())
                vbuffs.get<ArrayBufferType::ELEMENT>().bind();
        }

        void unbind()
        {
            unbind_self();
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
            bind();
            vbuffs.get<ABT>().create();
        }
        
        VABuffers& buffs()
        {
            return vbuffs;
        }

        std::vector<VABuffer<ArrayBufferType::VERTEX>>& get_oths() 
        {
            return oths;
        }

        const std::vector<VABuffer<ArrayBufferType::VERTEX>>& get_oths() const
        {
            return oths;
        }

        VABuffer<ArrayBufferType::VERTEX>& create_one()
        {
            VABuffer<ArrayBufferType::VERTEX> buff;
            buff.create();
            bind_self();
            buff.bind();
            oths.push_back(std::move(buff));
            return oths.back();
        }

        void vertex_attr_div(GLuint idx,GLuint div)
        {
            glVertexAttribDivisor(idx,div);
        }

        template<size_t B,size_t Df,size_t ... Div>
        void vertex_attr_div()
        {
            glVertexAttribDivisor(B,Df);
            if constexpr(sizeof...(Div) > 0)
            {
                vertex_attr_div<B + 1,Div...>();
            }
        }

    private:
        Glid id = 0;  
        VABuffers vbuffs;
        std::vector<VABuffer<ArrayBufferType::VERTEX>> oths;
    };
} // namespace gld
