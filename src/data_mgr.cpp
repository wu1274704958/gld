#include <data_mgr.hpp>
#include <resource_mgr.hpp>
#include <log.hpp>
#include <sundry.hpp>
#include <program.hpp>
#include <serialization.hpp>
#include <component.h>
#include <comps/Material.hpp>
#include <assimp/scene.h>
#include <fileop.hpp>

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


std::string gld::LoadTexture2D::key_from_args(gld::LoadTexture2D::ArgsTy args)
{
    std::string res = std::move(std::get<0>(args));
    res += "#";
    res += wws::to_string(std::get<1>(args));
    return res;
}
std::string gld::LoadTexture2D::key_from_args(std::tuple<const char*,int> args)
{
    std::string res = std::get<0>(args);
    res += "#";
    res += wws::to_string(std::get<1>(args));
    return res;
}
gld::LoadTexture2D::RealRetTy   gld::LoadTexture2D::load(gld::LoadTexture2D::ArgsTy args)
{
    dbg::log << "data mgr @V@"_E ;
    auto [path,flag] = args;
    auto image = ResMgrWithGlslPreProcess::instance()->load<ResType::image>(std::move(path),flag);
    bool s = false;
    std::shared_ptr<Texture<TexType::D2>> res;
    dbg::log << "LoadTexture2D " << (bool)image <<dbg::endl;
    if(image)
    {
        res = std::shared_ptr<Texture<TexType::D2>>(new Texture<TexType::D2>());
        dbg::log << "LoadTexture2D " << image->width <<" "<< image->height <<dbg::endl;
        res->create();
        res->bind();
        res->tex_image(0,image->gl_format(),0,image->gl_format(),image->data,image->width,image->height);
        res->unbind();
        s = true;
        return make_result(s,std::move(res));
    }
    return make_result(s,std::move(res));
}
gld::LoadTexture2D::RealRetTy   gld::LoadTexture2D::load(std::tuple<const char*,int> args)
{
    return load(std::make_tuple(std::string(std::get<0>(args)),std::get<1>(args)));
}

std::string gld::LoadSceneNode::key_from_args(gld::LoadSceneNode::ArgsTy args)
{
   std::string res = std::move(std::get<0>(args));
   res += "#";
   res += wws::to_string(std::get<1>(args));
   return res;
}
std::string gld::LoadSceneNode::key_from_args(std::tuple<const char*,unsigned int,const char*,const char*> args)
{
   std::string res = std::get<0>(args);
   res += "#";
   res += wws::to_string(std::get<1>(args));
   return res;
}

gld::LoadSceneNode::RealRetTy gld::LoadSceneNode::load(std::tuple<const char*,unsigned int,const char*,const char*> args)
{
    auto [a,b,c,d] = args;
    return load(std::make_tuple(std::string(a),b,std::string(c),std::string(d)));
}

std::shared_ptr<gld::Node<gld::Component>> process_mesh(const aiScene* scene,aiMesh* mesh,
    std::string& parent_path)
{
    return std::shared_ptr<gld::Node<gld::Component>>();
}

std::shared_ptr<gld::Node<gld::Component>> process_node(aiNode *ai_node,const aiScene* scene,
    std::string& parent_path)
{
    auto node = std::shared_ptr<gld::Node<gld::Component>>(new gld::Node<gld::Component>());
    node->add_comp<gld::Transform>(std::make_shared<gld::Transform>());
    for(unsigned int i = 0;i < ai_node->mNumMeshes;++i)
    {
        node->add_child(process_mesh(scene,scene->mMeshes[ ai_node->mMeshes[i] ],parent_path));
    }

    for(unsigned int i = 0;i < ai_node->mNumChildren;++i)
    {
        node->add_child(process_node(ai_node->mChildren[i],scene,parent_path));
    }
    return node;
}

gld::LoadSceneNode::RealRetTy gld::LoadSceneNode::load(gld::LoadSceneNode::ArgsTy args)
{
    dbg::log << "LoadSceneNode @V@"_E ;
    auto [path,flag,vp,fp] = args;
    auto ai = ResMgrWithGlslPreProcess::instance()->load<ResType::model>(path,flag);
    bool s = false;
    std::shared_ptr<Node<Component>> res;
    std::string parent_path = wws::get_parent(path);
    if(ai)
    {   
        if(auto node = process_node(ai->GetScene()->mRootNode,ai->GetScene(),parent_path);node)
        {
            return make_result(s,std::move(node));
        }
    }
    return make_result(s,std::move(res));
}

