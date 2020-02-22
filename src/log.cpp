#include <log.hpp>

namespace dbg{
    Log<std::ostream> log = Log<std::ostream>(std::cout);
}