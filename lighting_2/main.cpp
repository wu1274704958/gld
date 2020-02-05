#include <resource_mgr.hpp>
#include <FindPath.hpp>

namespace fs = std::filesystem;

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
    fs::path root = wws::find_path(3,"res",true);
    ResourceMgr<'/'> res_mgr(std::move(root));

    res_mgr.uri_to_path("shader/base.vs");

    return 0;
}