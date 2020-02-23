#pragma once

#include <filesystem>
#include <comm.hpp>
#include <glsl_preprocess.hpp>
#include <memory>
#ifdef PF_ANDROID
#include <EGLCxt.h>
#include <android/asset_manager.h>
#define Loge(f,...) __android_log_print(ANDROID_LOG_ERROR,"ResMgr @V@",f,##__VA_ARGS__)
#endif

namespace gld{

    enum class ResType{
        text = 0x0,
        image,
        model
    };

#ifndef PF_ANDROID
    using PathTy = std::filesystem::path;
#else
    using AndroidCxtPtrTy = std::shared_ptr<EGLCxt>;
    using PathTy = std::string;
#endif

#ifndef PF_ANDROID
    template <class T,typename ...Args>											
    using has_load_func_t = decltype(T::load(std::declval<PathTy>(),std::declval<Args>()...));
#else
    template <class T,typename ...Args>
    using has_load_func_t = decltype(T::load(std::declval<AndroidCxtPtrTy>(),std::declval<PathTy>(),std::declval<Args>()...));
#endif
    template <typename T,typename ...Args>
    using has_load_func_vt = wws::is_detected<has_load_func_t,T,Args...>;
#ifndef PF_ANDROID
    template <class T>
    using has_load_func2_t = decltype(T::load(std::declval<PathTy>()));
#else
    template <class T>
    using has_load_func2_t = decltype(T::load(std::declval<AndroidCxtPtrTy>(),std::declval<PathTy>()));
#endif
    template <typename T>
    using has_load_func2_vt = wws::is_detected<has_load_func2_t, T>;

    template <class T>											
    using has_ret_type_t = typename T::RetTy;

    template <typename T>
    using has_ret_type_vt = wws::is_detected<has_ret_type_t,T>;

     template <class T>											
    using has_args_type_t = typename T::ArgsTy;

    template <typename T>
    using has_args_type_vt = wws::is_detected<has_args_type_t,T>;

    template<ResType ty,typename T>
    struct ResLoadPlugTy
    {
        constexpr static size_t res_type = static_cast<size_t>(ty);
        using type = T;

        static_assert(has_ret_type_vt<T>::value,"this type must has RetTy!!!");
        static_assert(has_args_type_vt<T>::value,"this type must has ArgsTy!!!");
        
        using Ret = typename T::RetTy;
        using Args = typename T::ArgsTy;
        static_assert(
            has_load_func_vt<T,Args>::value || (has_load_func2_vt<T>::value && std::is_same_v<Args,void>),
            "this type must has load func!!!");
    };

    template<size_t Rt,typename ...Ts>
    struct MapResPlug;

    template<size_t Rt,typename Fir,typename ...Ts>
    struct MapResPlug<Rt,Fir,Ts...>
    {
        constexpr static decltype(auto) func()
        {
            if constexpr (Rt == Fir::res_type)
            {
                using T = typename Fir::type;
                return std::declval<T>();
            }
            else
            {
                using T = typename MapResPlug<Rt, Ts...>::type;
                if constexpr (std::is_same_v<T, void>)
                {
                    static_assert("Error Type!!!");
                }
                return std::declval<T>();
            }
        }
        using type = typename std::remove_reference_t<decltype(func())>;
    };

    template<size_t Rt>
    struct MapResPlug<Rt>
    {
        using type = void;
    };

    template<char Separator,typename ...Plugs>
    class ResourceMgr{
    public:
#ifndef PF_ANDROID
        ResourceMgr(const char* _root) : root(_root)
        {
            if(!std::filesystem::exists(root))
                throw std::runtime_error("This root not exists!!!");
        }

        ResourceMgr(PathTy _root) : root(_root)
        {
            if(!std::filesystem::exists(root))
                throw std::runtime_error("This root not exists!!!");
        }
#else
        ResourceMgr(std::shared_ptr<EGLCxt> mgr)
        {
            if(!mgr)
                throw std::runtime_error("EGLCxt is nullptr!!!");
            this->mgr = std::move(mgr);
        }
#endif

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

        template<ResType Rt,typename Uri>
        auto
            load(Uri&& uri,typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type::ArgsTy args)
            ->typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type::RetTy
        {
            using Ty = typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type;
#ifndef PF_ANDROID
            return Ty::load(to_path(std::forward<Uri>(uri)),std::forward<typename Ty::ArgsTy>(args));
#else
            return Ty::load(mgr,to_path(std::forward<Uri>(uri)),std::forward<typename Ty::ArgsTy>(args));
#endif
        }

        template<ResType Rt, typename Uri>
        auto
            load(Uri&& uri)
            ->typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type::RetTy
        {
            static_assert(std::is_same_v<typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type::ArgsTy, void>,
                "Load plug args type must be void!!!");
            using Ty = typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type;
#ifndef PF_ANDROID
            return Ty::load(to_path(std::forward<Uri>(uri)));
#else
            return Ty::load(mgr,to_path(std::forward<Uri>(uri)));
#endif
        }
    protected:
#ifndef PF_ANDROID
        PathTy root;
#else
        std::shared_ptr<EGLCxt> mgr;
#endif
    };

    struct LoadText
    {
        using RetTy = std::unique_ptr<std::string>;
        using ArgsTy = void;
#ifndef PF_ANDROID
        static std::unique_ptr<std::string> load(PathTy p);
#else
        static std::unique_ptr<std::string> load(AndroidCxtPtrTy,PathTy p);
#endif
    };

    struct LoadTextWithGlslPreprocess
    {
        using RetTy = std::unique_ptr<std::string>;
        using ArgsTy = void;
#ifndef PF_ANDROID
        static std::unique_ptr<std::string> load(PathTy p);
#else
        static std::unique_ptr<std::string> load(AndroidCxtPtrTy,PathTy p);
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
        using RetTy = std::unique_ptr<StbImage>;
        using ArgsTy = int;
#ifndef PF_ANDROID
        static std::unique_ptr<StbImage> load(PathTy p,int req_comp);
#else
        static std::unique_ptr<StbImage> load(AndroidCxtPtrTy,PathTy p,int req_comp);
#endif
    };

    typedef ResourceMgr<'/', ResLoadPlugTy<ResType::text, LoadText>,ResLoadPlugTy<ResType::image,LoadImage>> DefResMgr;
    typedef ResourceMgr<'/', ResLoadPlugTy<ResType::text, LoadTextWithGlslPreprocess>,ResLoadPlugTy<ResType::image,LoadImage>> ResMgrWithGlslPreProcess;
}

#ifdef PF_ANDROID
#undef Loge
#endif