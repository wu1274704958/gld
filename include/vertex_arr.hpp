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
            return map_gl_type_enum_sub<0, 
                Pair<char, 0x1400>,
                Pair<unsigned char, 0x1401>,
                Pair<short, 0x1402>,
                Pair<unsigned short, 0x1403>,
                Pair<int, 0x1404>,
                Pair<unsigned int, 0x1405>,
                Pair<float, 0x1406>,
                Pair<double, 0x140A>>();
        }
        template<size_t Idx,typename F,typename ...Ps>
        static constexpr size_t map_gl_type_enum_sub()
        {
            if constexpr (std::is_same_v<Ty, F::template type>)
            {
                return F::template val;
            }
            else {
                if constexpr (sizeof...(Ps) > 0)
                {
                    return map_gl_type_enum_sub<Idx + 1, Ps...>();
                }
                else {
                    return 0;
                }
            }
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

        template<size_t Stride,size_t Idx,size_t Off,typename T,typename ...Ts>
        void vertex_attrib_pointer_sub()
        {
            glVertexAttribPointer(Idx,T::template len,T::template map_gl_type_enum(),T::template normalized,Stride,(void *)Off);
            glEnableVertexAttribArray(Idx);
            if constexpr(sizeof...(Ts) > 0)
            {
                vertex_attrib_pointer_sub<Stride,Idx + 1,sizeof(T::template type) * T::template len,Ts...>();
            }
        }

        template<typename T, typename ...Ts>
        static constexpr size_t vertex_attrib_stride()
        {
            constexpr size_t res = sizeof(T::template type) * T::template len;
            if constexpr (sizeof...(Ts) > 0)
            {
                return res + vertex_attrib_stride<Ts...>();
            }
            else {
                return res;
            }
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
            bind();
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
