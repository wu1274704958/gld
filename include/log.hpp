#pragma once

#include <iostream>

namespace dbg{

    template<typename Stream>
    struct Log
    {
        Log(Stream& stream):stream(stream){}
        template<typename T>
        Log<Stream>& operator<<(T&& t)
        {
            stream << std::forward<T>(t);
            return *this;
        }

        Log<Stream>& operator<<(
            std::basic_ostream<char, std::char_traits<char>>& (__cdecl *_Pfn)(std::basic_ostream<char, std::char_traits<char>>&)
        ){ 
            stream << _Pfn;
            return *this;
        }

        operator Stream& ()
        {
            return stream;
        }
           
        Stream& stream;
    };
    extern Log<std::ostream> log;
}