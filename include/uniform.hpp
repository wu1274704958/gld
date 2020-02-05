#pragma once
#include <program.hpp>
#include <tuple>

namespace gld
{

    enum class UT // uniform type
    { 
        Float = 0x0,
        Matrix4,
        Vec3,
        Vec4,
        Int,
        Sampler2D
    };

    template <UT Ut,typename T>
    struct UTData{
        constexpr static size_t UtId = static_cast<size_t>(Ut);
        using type = T;
    };

    template<size_t UtId,typename ...Ts>
    struct MapUTData;

    template<size_t UtId,typename Fir,typename ...Ts>
    struct MapUTData<UtId,Fir,Ts...>
    {
        constexpr static decltype(auto) func()
        {
            if constexpr (UtId == Fir::UtId)
            {
                using T = typename Fir::type;
                return std::declval<T>();
            }
            else
            {
                using T = typename MapUTData<UtId, Ts...>::type;
                if constexpr (std::is_same_v<T, void>)
                {
                    static_assert("Error Type!!!");
                }
                return std::declval<T>();
            }
        }
        using type = decltype(func());
    };

    template<size_t UtId>
    struct MapUTData<UtId>
    {
        using type = void;
    };

    struct UniformNotFound : public std::runtime_error
    {
        inline static UniformNotFound create(const std::string& key)
        {
            std::string msg("This program not found uniform ");
            msg += key;
            msg += "!";
            return UniformNotFound(msg.c_str());
        }
        UniformNotFound(const char* str) : runtime_error(str) {}
    };

    struct MatData
    {
        float* data;
        bool transpose;
        MatData() :data(nullptr), transpose(false) {}
        MatData(float* data) :data(data), transpose(false) {}
        MatData(float* data, bool transpose) :data(data), transpose(transpose) {}
    };

    template<UT Ut>
    class Uniform{
    public:
        using UTDataMapTy = typename std::remove_reference_t<typename MapUTData<static_cast<size_t>(Ut),
            UTData<UT::Float,float>,
            UTData<UT::Matrix4, MatData>,
            UTData<UT::Vec3,float*>,
            UTData<UT::Vec4,float*>,
            UTData<UT::Int,int>,
            UTData<UT::Sampler2D,int>
        >::type>;

        Uniform(std::string key, Program& program) :
            key(key),
            program(program)
        {}

        UTDataMapTy operator=(UTDataMapTy data)
        {
            const std::string& key_r = key;
            int id = program.uniform_id(key_r);
            if (id < 0)
                throw UniformNotFound::create(key_r);
            if constexpr (Ut == UT::Float)
            {
                glUniform1f(id, data);
            }else
            if constexpr (Ut == UT::Matrix4)
            {
                if(data.data)
                    glUniformMatrix4fv(id, 1, data.transpose,data.data);
            }
            else
            if constexpr (Ut == UT::Vec3)
            {
                glUniform3fv(id, 1, data);
            }
            else
            if constexpr (Ut == UT::Vec4)
            {
                glUniform4fv(id, 1, data);
            }
            else
            if constexpr (Ut == UT::Int || Ut == UT::Sampler2D)
            {
                glUniform1i(id, data);
            }
            return data;
        }
    protected:
        std::string key;
        Program& program;
    };
    
} // namespace gld

