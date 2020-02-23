#pragma once

#include <iostream>

namespace dbg{

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
            stream << std::forward<T>(t);
            return *this;
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
    extern Log<std::ostream> log;

    template <class _Elem, class _Traits>
    std::basic_ostream<_Elem, _Traits>& __CLRCALL_OR_CDECL endl(
    std::basic_ostream<_Elem, _Traits>& _Ostr) { // insert newline and flush stream
        _Ostr.put(_Ostr.widen('\n'));
        _Ostr.flush();
        return _Ostr;
    }
}