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
        Sampler2D,
        SamplerCube
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
            UTData<UT::Sampler2D,int>,
            UTData<UT::SamplerCube,int>
        >::type>;

        Uniform(std::string key, std::shared_ptr<Program> program) :
            key(key),
            program(std::move(program))
        {}

        Uniform(std::string key) :
            key(key)
        {}

        UTDataMapTy operator=(UTDataMapTy data)
        {
            const std::string& key_r = key;
            int id = program->uniform_id(key_r);
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
            if constexpr (Ut == UT::Int || Ut == UT::Sampler2D || Ut == UT::SamplerCube)
            {
                glUniform1i(id, data);
            }
            return data;
        }

        void attach_program(std::shared_ptr<Program> p)
        {
            program = std::move(p);
        }
    protected:
        std::string key;
        std::shared_ptr<Program> program;
    };

    template<typename  T>
    struct CustomUniform{
        using UTDataMapTy = T;

        CustomUniform(std::string key,std::shared_ptr<Program> p) : key(std::move(key)),program(std::move(p))
        {
            
        }
        CustomUniform(std::string key) : key(std::move(key))
        {
            
        }
        template<typename ...Args>
        CustomUniform(std::string key,std::shared_ptr<Program> p,Args&& ...args) :
            key(std::move(key)),program(p),
            data(std::forward<Args>(args)...)
        {
            
        }

        UTDataMapTy operator=(UTDataMapTy d)
        {
            data = d;
            return d;
        }

        operator UTDataMapTy& ()
        {
            return data;
        }

        UTDataMapTy& operator*()
        {
            return data;
        }

        virtual void sync(){}

        virtual ~CustomUniform(){}

        void attach_program(std::shared_ptr<Program> p)
        {
            program = std::move(p);
        }
    protected:
        UTDataMapTy data;
        std::string key;
        std::shared_ptr<Program> program;
    };
    
} // namespace gld

