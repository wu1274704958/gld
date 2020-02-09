#pragma once

#include <filesystem>
#include <comm.hpp>
#include <glsl_preprocess.hpp>


namespace gld{

    enum class ResType{
        text = 0x0,
        image,
        model
    };

    template <class T,typename ...Args>											
    using has_load_func_t = decltype(T::load(std::declval<std::filesystem::path>(),std::declval<Args>()...));

    template <typename T,typename ...Args>
    using has_load_func_vt = wws::is_detected<has_load_func_t,T,Args...>;

    template <class T>
    using has_load_func2_t = decltype(T::load(std::declval<std::filesystem::path>()));

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
        ResourceMgr(const char* _root) : root(_root)
        {
            if(!std::filesystem::exists(root))
                throw std::runtime_error("This root not exists!!!");
        }

        ResourceMgr(std::filesystem::path _root) : root(_root)
        {
            if(!std::filesystem::exists(root))
                throw std::runtime_error("This root not exists!!!");
        }

        std::filesystem::path to_path(std::string& uri) const
        {
            int b = 0,i = 0;
            std::filesystem::path res = root;
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

        std::filesystem::path to_path(const char* uri) const
        {
            std::string str(uri);
            return to_path(str);
        }

        std::filesystem::path to_path(std::string&& uri) const
        {
            return to_path(uri);
        }

        template<ResType Rt,typename Uri>
        auto
            load(Uri&& uri,typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type::ArgsTy args)
            ->typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type::RetTy
        {
            using Ty = typename MapResPlug<static_cast<size_t>(Rt),Plugs...>::type;
            return Ty::load(to_path(std::forward<Uri>(uri)),std::forward<typename Ty::ArgsTy>(args));
        }

        template<ResType Rt, typename Uri>
        auto
            load(Uri&& uri)
            ->typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type::RetTy
        {
            static_assert(std::is_same_v<typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type::ArgsTy, void>,
                "Load plug args type must be void!!!");
            using Ty = typename MapResPlug<static_cast<size_t>(Rt), Plugs...>::type;
            return Ty::load(to_path(std::forward<Uri>(uri)));
        }
    protected:
        std::filesystem::path root;
    };

    struct LoadText
    {
        using RetTy = std::unique_ptr<std::string>;
        using ArgsTy = void;
        static std::unique_ptr<std::string> load(std::filesystem::path p);
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
        static std::unique_ptr<StbImage> load(std::filesystem::path p,int req_comp);
    };

    typedef ResourceMgr<'/', ResLoadPlugTy<ResType::text, LoadText>,ResLoadPlugTy<ResType::image,LoadImage>> DefResMgr;
}