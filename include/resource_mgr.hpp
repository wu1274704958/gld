#pragma once

#include <filesystem>
#include <comm.hpp>
#include <glsl_preprocess.hpp>
#include <memory>
#ifdef PF_ANDROID
#include <fileop.hpp>
#include <EGLCxt.h>
#include <android/asset_manager.h>
#define Loge(f,...) __android_log_print(ANDROID_LOG_ERROR,"ResMgr @V@",f,##__VA_ARGS__)
#endif

#include <res_comm.hpp>
#include <res_cache_mgr.hpp>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

namespace gld{    

    template<char Separator,typename ...Plugs>
    class ResourceMgr{
    public:

#ifndef PF_ANDROID
        PathTy to_path(std::string& uri) const
        {
            int b = 0,i = 0;
            PathTy res = root;
            for(;i < uri.size();++i)
            {
                if(uri[i] == Separator)
                {
                    std::string t = uri.substr(b,i - b);
                    if(!t.empty())
                        res.append(t.c_str());
                    b = i + 1;
                }
            }
            if(b < i)
            {
                std::string t = uri.substr(b,i - b);
                if(!t.empty())
                    res.append(t.c_str());
            }
            if(!std::filesystem::exists(res))
                throw std::runtime_error("This file not exists!!!");
            return res;
        }
#else
        PathTy to_path(std::string& uri) const
        {
            return std::move(uri);
        }
#endif

        PathTy to_path(const char* uri) const
        {
            std::string str(uri);
            return to_path(str);
        }

        PathTy to_path(std::string&& uri) const
        {
            return to_path(uri);
        }

        std::string to_absolute_path(const PathTy& path) const
        {
#ifndef PF_ANDROID 
            return std::filesystem::absolute(path).string();
#else
            return wws::to_absolute(path);
#endif
        }

        template<ResType Rt,typename Uri>
        auto
            load(Uri&& uri,typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type::ArgsTy args)
            ->typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type::RetTy
        {
            using Ty = typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type;
            using ARGS_T = typename Ty::ArgsTy;
            using RET_T = typename Ty::RetTy;
            
            auto path = to_path(std::forward<Uri>(uri));

            auto absolute_path = to_absolute_path(path);

            if constexpr(!std::is_same_v<ARGS_T,void> && has_format_args_func_vt<Ty,ARGS_T>::value)
            {
                absolute_path += Ty::format_args(std::forward<ARGS_T>(args));
            }

            if(ResCacheMgr<Plugs...>::instance()->template has<static_cast<size_t>(Rt)>(absolute_path))
            {
                return ResCacheMgr<Plugs...>::instance()->template get<static_cast<size_t>(Rt)>(absolute_path);
            }
#ifndef PF_ANDROID
            auto [success,res] = Ty::load(path,std::forward<typename Ty::ArgsTy>(args));
#else
            auto [success,res] = Ty::load(mgr,path,std::forward<typename Ty::ArgsTy>(args));
#endif
            if(success)
                ResCacheMgr<Plugs...>::instance()->template cache<static_cast<size_t>(Rt)>(absolute_path,res);
            return res;
        }

        template<ResType Rt, typename Uri>
        auto
            load(Uri&& uri)
            ->typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type::RetTy
        {

            static_assert(std::is_same_v<typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type::ArgsTy, void> || 
                has_default_args_func_vt<typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type>::value,"Load plug args type must be void!!!");

            using Ty = typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type;
            using ARGS_T = typename Ty::ArgsTy;
            using RET_T = typename Ty::RetTy;

            if constexpr(!std::is_same_v<ARGS_T,void>)
            {
                if constexpr( has_default_args_func_vt<Ty>::value)
                {
                    return load<Rt>(std::forward<Uri>(uri),Ty::default_args());
                }
            }else{

                auto path = to_path(std::forward<Uri>(uri));
    
                auto absolute_path = to_absolute_path(path);
    
                if(ResCacheMgr<Plugs...>::instance()->template has<static_cast<size_t>(Rt)>(absolute_path))
                {
                    return ResCacheMgr<Plugs...>::instance()->template get<static_cast<size_t>(Rt)>(absolute_path);
                }
    #ifndef PF_ANDROID
                auto [success,res] = Ty::load(path);
    #else
                auto [success,res] = Ty::load(mgr,path);
    #endif
                if(success)
                    ResCacheMgr<Plugs...>::instance()->template cache<static_cast<size_t>(Rt)>(absolute_path,res);
                return res;
            }
        }
        template<ResType Rt,typename Uri>
        decltype(auto) rm_cache(Uri&& uri)
        {
            using Ty = typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type;

            auto path = to_path(std::forward<Uri>(uri));

            auto absolute_path = to_absolute_path(path);

            return ResCacheMgr<Plugs...>::instance()->template rm_cache<static_cast<size_t>(Rt)>(absolute_path);
        }

        inline static std::shared_ptr<ResourceMgr<Separator,Plugs...>> instance()
        {
            return self;
        }
        template<typename T>
        inline static std::shared_ptr<ResourceMgr<Separator,Plugs...>> create_instance(T&& t)
        {
            self = std::shared_ptr<ResourceMgr<Separator,Plugs...>>(new ResourceMgr<Separator,Plugs...>(std::forward<T>(t)));
            return self;
        }
    protected:
#ifndef PF_ANDROID
        PathTy root;
#else
        std::shared_ptr<EGLCxt> mgr;
#endif

private:    
    inline static std::shared_ptr<ResourceMgr<Separator,Plugs...>> self;
#ifndef PF_ANDROID
        ResourceMgr<Separator,Plugs...>(const char* _root) : root(_root)
        {
            if(!std::filesystem::exists(root))
                throw std::runtime_error("This root not exists!!!");
        }

        ResourceMgr<Separator,Plugs...>(PathTy _root) : root(std::move(_root))
        {
            if(!std::filesystem::exists(root))
                throw std::runtime_error("This root not exists!!!");
        }
#else
        ResourceMgr<Separator,Plugs...>(std::shared_ptr<EGLCxt> mgr)
        {
            if(!mgr)
                throw std::runtime_error("EGLCxt is nullptr!!!");
            this->mgr = std::move(mgr);
        }
#endif
    };

    struct LoadText
    {
        using RetTy = std::shared_ptr<std::string>;
        using ArgsTy = void;
        using RealRetTy = std::tuple<bool,RetTy>;
#ifndef PF_ANDROID
        static RealRetTy load(PathTy p);
#else
        static RealRetTy load(AndroidCxtPtrTy,PathTy p);
#endif
    };

    struct LoadTextWithGlslPreprocess
    {
        using RetTy = std::shared_ptr<std::string>;
        using ArgsTy = void;
        using RealRetTy = std::tuple<bool,RetTy>;
#ifndef PF_ANDROID
        static RealRetTy load(PathTy p);
#else
        static RealRetTy load(AndroidCxtPtrTy,PathTy p);
#endif
    };

    struct StbImage {
        unsigned char* data = nullptr;
        int width = 0;
        int height = 0;
        int channel = 0;
        unsigned int gl_format();
        ~StbImage();
    };

    struct LoadImage
    {
        using RetTy = std::shared_ptr<StbImage>;
        using ArgsTy = int;
        using RealRetTy = std::tuple<bool,RetTy>;
#ifndef PF_ANDROID
        static RealRetTy load(PathTy p,int req_comp);
#else
        static RealRetTy load(AndroidCxtPtrTy,PathTy p,int req_comp);
#endif
    };

    struct LoadScene
    {
        using RetTy = std::shared_ptr<Assimp::Importer>;
        using ArgsTy = unsigned int;
        using RealRetTy = std::tuple<bool,RetTy>;
        static std::string format_args(ArgsTy flag);
        static ArgsTy default_args();
#ifndef PF_ANDROID
        static RealRetTy load(PathTy p,ArgsTy flag);
#else
        static RealRetTy load(AndroidCxtPtrTy,PathTy p,ArgsTy flag);
#endif
    };

    typedef ResourceMgr<'/', ResLoadPlugTy<ResType::text, LoadText>,
        ResLoadPlugTy<ResType::image,LoadImage>,
        ResLoadPlugTy<ResType::model,LoadScene>> DefResMgr;
    typedef ResourceMgr<'/', ResLoadPlugTy<ResType::text, LoadTextWithGlslPreprocess>,
        ResLoadPlugTy<ResType::image,LoadImage>,
        ResLoadPlugTy<ResType::model,LoadScene>> ResMgrWithGlslPreProcess;
}

#ifdef PF_ANDROID
#undef Loge
#endif