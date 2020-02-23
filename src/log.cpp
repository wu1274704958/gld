#include <log.hpp>

#define DEF_operator_LOG_Priority(lit,ty) \
TagWarp operator "" _##lit(const char *col, size_t n)   \
{\
    return create_tw(col,n,LogPriority::ty);\
}

namespace dbg{
#ifndef PF_ANDROID 
    Log<std::ostream> log = Log<std::ostream>(std::cout);
#else
    ALogStream<TagWarp,int> android_log;
    Log<ALogStream<TagWarp,int>> log = Log<ALogStream<TagWarp,int>>(android_log);
#endif
    namespace literal
    {
        inline TagWarp create_tw(const char *col, size_t n,LogPriority lp)
        {
            TagWarp tw;
            tw.tag.resize(n);
            std::memcpy(tw.tag.data(),col,n);
            tw.prior = lp;
            return tw;
        }
        DEF_operator_LOG_Priority(DE,DEFAULT)
        DEF_operator_LOG_Priority(V,VERBOSE)
        DEF_operator_LOG_Priority(D,DEBUG)
        DEF_operator_LOG_Priority(I,INFO)
        DEF_operator_LOG_Priority(W,WARN)
        DEF_operator_LOG_Priority(E,ERROR)
        DEF_operator_LOG_Priority(F,FATAL)
        DEF_operator_LOG_Priority(S,SILENT)
    }
}

#undef DEF_operator_LOG_Priority