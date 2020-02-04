#include <resource_mgr.hpp>

struct test
{
    using RetTy = int;
    using ArgsTy = int;
    static int load(int);
};

using namespace gld;

int main()
{
    ResLoadPlugTy<ResType::image,test> r;
    return 0;
}