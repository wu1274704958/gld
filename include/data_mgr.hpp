#pragma once

#include <data_comm.hpp>
#include <res_cache_mgr.hpp>
#include <texture.hpp>

namespace gld
{
    template<typename ...Plugs>
    class DataMgr{
    public:

        template<DataType Rt>
        auto
            load(typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type::ArgsTy args)
            ->typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type::RetTy
        {
            using Ty = typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type;
            using ARGS_T = typename Ty::ArgsTy;
            using RET_T = typename Ty::RetTy;

            auto key = Ty::key_from_args(args);

            if(ResCacheMgr<Plugs...>::instance()->template has<static_cast<size_t>(Rt)>(key))
            {
                return ResCacheMgr<Plugs...>::instance()->template get<static_cast<size_t>(Rt)>(key);
            }

            auto [success,res] = Ty::load(std::forward<typename Ty::ArgsTy>(args));

            if(success)
                ResCacheMgr<Plugs...>::instance()->template cache<static_cast<size_t>(Rt)>(key,res);
            return res;
        }

        template<DataType Rt>
        auto load()
            ->typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type::RetTy
        {

            static_assert(data_ck::has_default_args_func_vt<typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type>::value,
                "Load plug args type must has default_args function!!!");

            using Ty = typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type;
            using ARGS_T = typename Ty::ArgsTy;
            using RET_T = typename Ty::RetTy;

            return load<Rt>(Ty::default_args());
        }

        template<DataType Rt,typename ...Args>
        decltype(auto)
            load(Args&&... args)
        {
            using Ty = typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type;
            using ARGS_T = typename Ty::ArgsTy;
            using RET_T = typename Ty::RetTy;

            static_assert(data_ck::has_load_func3_vt<Ty,Args...>::value,"This load plug not has tuple load function!!!");

            auto key = Ty::key_from_args(std::make_tuple(std::forward<Args>(args)...));

            if(ResCacheMgr<Plugs...>::instance()->template has<static_cast<size_t>(Rt)>(key))
            {
                return ResCacheMgr<Plugs...>::instance()->template get<static_cast<size_t>(Rt)>(key);
            }

            auto [success,res] = Ty::load(std::make_tuple(std::forward<Args>(args)...));

            if(success)
                ResCacheMgr<Plugs...>::instance()->template cache<static_cast<size_t>(Rt)>(key,res);
            return res;
        }

        template<DataType Rt>
        decltype(auto) rm_cache(typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type::ArgsTy args)
        {
            using Ty = typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type;

            auto key = Ty::key_from_args(args);

            return ResCacheMgr<Plugs...>::instance()->template rm_cache<static_cast<size_t>(Rt)>(key);
        }

        template<DataType Rt,typename ...Args>
        decltype(auto) rm_cache(Args&&... args)
        {
            using Ty = typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type;
            
            return rm_cache(std::make_tuple(std::forward<Args>(args)...));
        }

        template<DataType Rt>
        decltype(auto) rm_cache_def()
        {
            using Ty = typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type;
            static_assert(data_ck::has_default_args_func_vt<Ty>::value , "must has default_args function!!!");

            return rm_cache(Ty::default_args());
        }

        inline static std::shared_ptr<DataMgr<Plugs...>> instance()
        {

            if(!self) 
                self = std::shared_ptr<DataMgr<Plugs...>>(new DataMgr<Plugs...>());
            return self;
        }
        template<typename T>
        inline static std::shared_ptr<DataMgr<Plugs...>> create_instance(T&& t)
        {
            self = std::shared_ptr<DataMgr<Plugs...>>(new DataMgr<Plugs...>(std::forward<T>(t)));
            return self;
        }

        void clear_all()
        {
            ResCacheMgr<Plugs...>::instance()->clear_all();
        }
protected:
private:    
    inline static std::shared_ptr<DataMgr<Plugs...>> self;

        DataMgr<Plugs...>() {}
    };

    class Program;

    struct LoadProgram{
        using RetTy = std::shared_ptr<Program>;
        using ArgsTy = std::tuple<std::string,std::string>;
        using RealRetTy = std::tuple<bool,RetTy>;
        static std::string key_from_args(ArgsTy args);
        static std::string key_from_args(std::tuple<const char*,const char*> args);
        static RealRetTy load(ArgsTy args);
        static RealRetTy load(std::tuple<const char*,const char*> args);
    };

    struct LoadProgramWithGeom{
        using RetTy = std::shared_ptr<Program>;
        using ArgsTy = std::tuple<std::string,std::string,std::string>;
        using RealRetTy = std::tuple<bool,RetTy>;
        static std::string key_from_args(ArgsTy args);
        static std::string key_from_args(std::tuple<const char*,const char*,const char*> args);
        static RealRetTy load(ArgsTy args);
        static RealRetTy load(std::tuple<const char*,const char*,const char*> args);
    };

    struct LoadTexture2D{
        using RetTy = std::shared_ptr<Texture<TexType::D2>>;
        using ArgsTy = std::tuple<std::string,int>;
        using RealRetTy = std::tuple<bool,RetTy>;
        static std::string key_from_args(ArgsTy args);
        static std::string key_from_args(std::tuple<const char*,int> args);
        static RealRetTy load(ArgsTy args);
        static RealRetTy load(std::tuple<const char*,int> args);
    };

    class CubeTexture;

    struct LoadTextureCube{
        using RetTy = std::shared_ptr<CubeTexture>;
        using ArgsTy = std::tuple<std::string,std::string,int>;
        using RealRetTy = std::tuple<bool,RetTy>;
        static std::string key_from_args(ArgsTy args);
        static std::string key_from_args(std::tuple<const char*,const char*,int> args);
        static RealRetTy load(ArgsTy args);
        static RealRetTy load(std::tuple<const char*,const char*,int> args);
    };

    struct Component;
    template<typename Comp>
    struct Node;

    enum class SceneLoadMode : uint32_t
    {
        Default = 0x0,
        NoMaterial = 0x1,
        HasGeometry = 0x2,
        HasGeometry_NoMaterial = 0x3
    };

    template<SceneLoadMode M>
    struct LoadSceneNode{
        constexpr static SceneLoadMode LoadMode = M;
        using RetTy = std::shared_ptr<Node<Component>>;
        using ArgsTy = std::tuple<std::string,unsigned int,std::string,std::string>;
        using RealRetTy = std::tuple<bool,RetTy>;
        static std::string key_from_args(ArgsTy args);
        static std::string key_from_args(std::tuple<const char*,unsigned int,const char*,const char*> args);
        static RealRetTy load(ArgsTy args);
        static RealRetTy load(std::tuple<const char*,unsigned int,const char*,const char*> args);
    };

    template<SceneLoadMode M>
    struct LoadSceneNodeWithGeom{
        constexpr static SceneLoadMode LoadMode = M;
        using RetTy = std::shared_ptr<Node<Component>>;
        using ArgsTy = std::tuple<std::string,unsigned int,std::string,std::string,std::string>;
        using RealRetTy = std::tuple<bool,RetTy>;
        static std::string key_from_args(ArgsTy args);
        static std::string key_from_args(std::tuple<const char*,unsigned int,const char*,const char*,const char*> args);
        static RealRetTy load(ArgsTy args);
        static RealRetTy load(std::tuple<const char*,unsigned int,const char*,const char*,const char*> args);
    };

    typedef DataMgr<DataLoadPlugTy<DataType::Program,LoadProgram>,
        DataLoadPlugTy<DataType::Texture2D,LoadTexture2D>,
        DataLoadPlugTy<DataType::Scene,LoadSceneNode<SceneLoadMode::Default>>,
        DataLoadPlugTy<DataType::SceneNoMaterial,LoadSceneNode<SceneLoadMode::NoMaterial>>,
        DataLoadPlugTy<DataType::TextureCube,LoadTextureCube>,
        DataLoadPlugTy<DataType::ProgramWithGeometry,LoadProgramWithGeom>,
        DataLoadPlugTy<DataType::SceneWithGeometry,LoadSceneNodeWithGeom<SceneLoadMode::HasGeometry>>,
        DataLoadPlugTy<DataType::SceneWithGeometryNoMaterial,LoadSceneNodeWithGeom<SceneLoadMode::HasGeometry_NoMaterial>>
        > DefDataMgr;
} // namespace gld
