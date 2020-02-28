#include <data_mgr.hpp>
#include <resource_mgr.hpp>
#include <log.hpp>
#include <sundry.hpp>
#include <program.hpp>
using namespace dbg::literal;

std::string gld::LoadProgram::key_from_args(gld::LoadProgram::ArgsTy args)
{
    std::string res = std::move(std::get<0>(args));
    res += "#";
    res += std::get<1>(args);
    return res;
}

std::string gld::LoadProgram::key_from_args(std::tuple<const char*,const char*> args)
{
    std::string res = std::get<0>(args);
    res += "#";
    res += std::get<1>(args);
    return res;
}

gld::LoadProgram::RealRetTy gld::LoadProgram::load(std::tuple<const char*,const char*> args)
{
    return load(std::make_tuple(std::string(std::get<0>(args)),std::string(std::get<1>(args))));
}

gld::LoadProgram::RealRetTy gld::LoadProgram::load(gld::LoadProgram::ArgsTy args)
{
    dbg::log << "data mgr @V@"_E ;

    bool s = false;
    std::shared_ptr<Program> res;

    Shader<ShaderType::VERTEX> vertex;
    Shader<ShaderType::FRAGMENT> frag;

    auto vs_str = ResMgrWithGlslPreProcess::instance()->load<ResType::text>(std::get<0>(args));
    auto fg_str = ResMgrWithGlslPreProcess::instance()->load<ResType::text>(std::get<1>(args));
    if(vs_str && fg_str)
    {

        auto vs_p = vs_str.get()->c_str();
        auto fg_p = fg_str.get()->c_str();

        try {
            sundry::compile_shaders<100>(
                GL_VERTEX_SHADER, &vs_p, 1, (GLuint*)vertex,
                GL_FRAGMENT_SHADER, &fg_p, 1, (GLuint*)frag
            );
        }
        catch (sundry::CompileError e)
        {
            dbg::log << "compile failed " << e.what() << dbg::endl;
            return make_result(s,std::move(res));
        }
        catch (std::exception e)
        {
            dbg::log << e.what() << dbg::endl;
            return make_result(s,std::move(res));
        }
        res = std::shared_ptr<Program>(new Program());
        res->cretate();
        res->attach_shader(std::move(vertex));
        res->attach_shader(std::move(frag));
        res->link();
        s = true;
        return make_result(s,std::move(res));
    }
    return make_result(s,std::move(res));
}