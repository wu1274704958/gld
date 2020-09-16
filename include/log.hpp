#pragma once

#include <iostream>
#include <serialization.hpp>
#ifdef PF_ANDROID
#include <android/log.h>
#define Loge(f,...) __android_log_print(ANDROID_LOG_ERROR,"log.hpp @V@",f,##__VA_ARGS__)
#endif
#ifdef ERROR
#undef ERROR
#endif
namespace dbg{

    enum class LogPriority : int
    {
        UNKNOWN = 0,
        DEFAULT,
        VERBOSE,
        DEBUG,
        INFO,
        WARN,
        ERROR,
        FATAL,
        SILENT
    };

    struct TagWarp{
        std::string tag;
        LogPriority prior;

        TagWarp()
        {
            prior = LogPriority::VERBOSE;
        }
    };
    
    namespace literal{
        TagWarp operator "" _E(const char *col, size_t n);
        TagWarp operator "" _DE(const char *col, size_t n);
        TagWarp operator "" _V(const char *col, size_t n);
        TagWarp operator "" _D(const char *col, size_t n);
        TagWarp operator "" _I(const char *col, size_t n);
        TagWarp operator "" _W(const char *col, size_t n);
        TagWarp operator "" _F(const char *col, size_t n);
        TagWarp operator "" _S(const char *col, size_t n);
    }

    template<typename Tw,typename Unuse>
    struct ALogStream{
        using U = Unuse;

        template<typename T>
        ALogStream& operator<<(T&& t)
        {
            if constexpr(std::is_same_v<std::remove_reference_t<std::remove_cv_t<T>>,TagWarp>)
            {
                tw = std::move(t);
            }else if constexpr (std::is_same_v<std::string,std::remove_reference_t<std::remove_cv_t<T>>>)
            {
                cache += std::forward<T>(t);
            }else if constexpr (std::is_same_v<const char*,std::decay_t<T>>)
            {
                cache += std::forward<T>(t);
            } else{
                cache += wws::to_string(std::forward<T>(t));
            }
            return *this;
        }

        ALogStream& operator<<(ALogStream& (__cdecl *_Pfn)(ALogStream&))
        {
            return _Pfn(*this);
        }

        ALogStream& flush()
        {
#ifdef PF_ANDROID
            __android_log_print(static_cast<int>(tw.prior),tw.tag.c_str(),"%s",cache.c_str());
            cache = "";
            return *this;
#endif
        }

        Tw tw;
        std::string cache;
    };

    template<typename T>
    struct Log;

    template<
        typename _Elem,typename _Traits,template <class T1,class T2> class Stream
    >
    struct Log<Stream<_Elem,_Traits>>
    {
        Log(Stream<_Elem,_Traits>& stream):stream(stream){}
        template<typename T>
        Log<Stream<_Elem,_Traits>>& operator<<(T&& t)
        {
            if constexpr(
                std::is_same_v<Stream<_Elem,_Traits>,std::basic_ostream<_Elem, _Traits>> && 
                std::is_same_v<std::remove_cv_t<T>,TagWarp>)
            {
                return *this;
            }else{
                stream << std::forward<T>(t);
                return *this;
            }
        }

        Log<Stream<_Elem,_Traits>>& operator<<(
            Stream<_Elem,_Traits>& (__cdecl *_Pfn)(Stream<_Elem,_Traits>&)
        ){ 
            stream << _Pfn;
            return *this;
        }

        operator Stream<_Elem,_Traits>& ()
        {
            return stream;
        }
           
        Stream<_Elem,_Traits>& stream;
    };

#ifndef PF_ANDROID

    extern Log<std::ostream> log;
#ifdef PF_WIN32
    template <class _Elem, class _Traits>
    std::basic_ostream<_Elem, _Traits>& __CLRCALL_OR_CDECL endl(
    std::basic_ostream<_Elem, _Traits>& _Ostr) { // insert newline and flush stream
        _Ostr.put(_Ostr.widen('\n'));
        _Ostr.flush();
        return _Ostr;
    }
#else
    template <class _CharT, class _Traits>
    inline _LIBCPP_INLINE_VISIBILITY
    std::basic_ostream<_CharT, _Traits>&
    endl(std::basic_ostream<_CharT, _Traits>& __os)
    {
        __os.put(__os.widen('\n'));
        __os.flush();
        return __os;
    }   
#endif
#else

    extern Log<ALogStream<TagWarp,int>> log;

    template <class _Elem, class _Traits>
    ALogStream<_Elem, _Traits>& __cdecl endl(
    ALogStream<_Elem, _Traits>& _Ostr) { // insert newline and flush stream
        _Ostr.flush();
        return _Ostr;
    }

#endif
}

#undef Loge