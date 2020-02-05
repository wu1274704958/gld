#include <resource_mgr.hpp>
#include <FindPath.hpp>
#include <dbg.hpp>

namespace fs = std::filesystem;



using namespace gld;

int main()
{
    fs::path root = wws::find_path(3,"res",true);
    DefResMgr res_mgr(std::move(root));

    auto vs_str = res_mgr.load<ResType::text>("lighting_2/base_vs.glsl");
    auto fg_str = res_mgr.load<ResType::text>("lighting_2/base_fg.glsl");
    

    return 0;
}