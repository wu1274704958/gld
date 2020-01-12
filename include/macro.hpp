#pragma once

#ifndef BuildStr

#define BuildStr(NS,name,str) \
namespace  NS { const char * name = #str ; }

#endif 