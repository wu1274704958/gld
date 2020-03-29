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
    std::string res = sundry::format_tup(args,'#');
    return res;
}

std::string gld::LoadProgram::key_from_args(std::tuple<const char*,const char*> args)
{
    std::string res = sundry::format_tup(args,'#');
    return res;
}

gld::LoadProgram::RealRetTy gld::LoadProgram::load(std::tuple<const char*,const char*> args)
{
    return load(std::make_tuple(std::string(std::get<0>(args)),std::string(std::get<1>(args))));
}

gld::LoadProgram::RealRetTy gld::LoadProgram::load(gld::LoadProgram::ArgsTy args)
{
    dbg::log << "data mgr @V@"_E << "load program " << dbg::endl;

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
        auto [succ,errstr] = res->check_link_state<200>();
        if(succ)
        {
            s = true;
            return make_result(s,std::move(res));
        }else{
            dbg::log << "link failed " << *errstr << dbg::endl;
            return make_result(s,nullptr);
        }
    }
    return make_result(s,std::move(res));
}

std::string gld::LoadProgramWithGeom::key_from_args(gld::LoadProgramWithGeom::ArgsTy args)
{
    return sundry::format_tup(args,'#');
}

std::string gld::LoadProgramWithGeom::key_from_args(std::tuple<const char*,const char*,const char*> args)
{
    return sundry::format_tup(args,'#');
}

gld::LoadProgramWithGeom::RealRetTy gld::LoadProgramWithGeom::load(std::tuple<const char*,const char*,const char*> args)
{
    return load(std::make_tuple(std::string(std::get<0>(args)),std::string(std::get<1>(args)),std::string(std::get<2>(args))));
}

gld::LoadProgramWithGeom::RealRetTy gld::LoadProgramWithGeom::load(gld::LoadProgramWithGeom::ArgsTy args)
{
    dbg::log << "data mgr @V@"_E << "load program " << dbg::endl;

    bool s = false;
    std::shared_ptr<Program> res;

    Shader<ShaderType::VERTEX> vertex;
    Shader<ShaderType::FRAGMENT> frag;
    Shader<ShaderType::GEOMETRY> gemo;

    auto vs_str = ResMgrWithGlslPreProcess::instance()->load<ResType::text>(std::get<0>(args));
    auto fg_str = ResMgrWithGlslPreProcess::instance()->load<ResType::text>(std::get<1>(args));
    auto ge_str = ResMgrWithGlslPreProcess::instance()->load<ResType::text>(std::get<2>(args));
    if(vs_str && fg_str && ge_str)
    {

        auto vs_p = vs_str.get()->c_str();
        auto fg_p = fg_str.get()->c_str();
        auto ge_p = ge_str.get()->c_str();

        try {
            sundry::compile_shaders<100>(
                GL_VERTEX_SHADER, &vs_p, 1, (GLuint*)vertex,
                GL_FRAGMENT_SHADER, &fg_p, 1, (GLuint*)frag,
                GL_GEOMETRY_SHADER, &ge_p, 1 ,(GLuint*)gemo
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
        res->attach_shader(std::move(gemo));
        res->link();
        auto [succ,errstr] = res->check_link_state<200>();
        if(succ)
        {
            s = true;
            return make_result(s,std::move(res));
        }else{
            dbg::log << "link failed " << *errstr << dbg::endl;
            return make_result(s,nullptr);
        }
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
//---------------------------------------------------------------------------------------------------------------------
template<gld::SceneLoadMode M>
std::string gld::LoadSceneNode<M>::key_from_args(gld::LoadSceneNode<M>::ArgsTy args)
{
   std::string res = std::move(std::get<0>(args));
   res += "#";
   res += wws::to_string(std::get<1>(args));
   return res;
}
template<gld::SceneLoadMode M>
std::string gld::LoadSceneNode<M>::key_from_args(std::tuple<const char*,unsigned int,const char*,const char*> args)
{
   std::string res = std::get<0>(args);
   res += "#";
   res += wws::to_string(std::get<1>(args));
   return res;
}

template<gld::SceneLoadMode M>
typename gld::LoadSceneNode<M>::RealRetTy gld::LoadSceneNode<M>::load(std::tuple<const char*,unsigned int,const char*,const char*> args)
{
    auto [a,b,c,d] = args;
    return load(std::make_tuple(std::string(a),b,std::string(c),std::string(d)));
}


std::shared_ptr<gld::Texture<gld::TexType::D2>> get_material_tex(std::string parent,aiMaterial *material,aiTextureType ty,int i)
{
    aiString path;
    material->GetTexture(ty,i,&path);
    dbg::log << "get_material_tex @V@"_E <<"path " << path.C_Str() << " count " << material->GetTextureCount(ty) << dbg::endl;
    if(path.length > 0)
    {
        parent += '/';
        parent += path.C_Str();
        dbg::log <<"parent " << parent << dbg::endl;
        return gld::DefDataMgr::instance()->load<gld::DataType::Texture2D>(parent,0);
    }
    return std::shared_ptr<gld::Texture<gld::TexType::D2>>();
}

template<gld::SceneLoadMode M,typename ...Args>
std::shared_ptr<gld::Node<gld::Component>> process_mesh(const aiScene* scene,aiMesh* mesh,
    std::string& parent_path,std::string& vp,std::string& fp,std::tuple<Args...>& args)
{
    constexpr uint32_t ThisMode = static_cast<uint32_t>(M);

    std::shared_ptr<gld::Node<gld::Component>> res = std::make_shared<gld::Node<gld::Component>>();

    std::vector<gld::def::Vertex> vertices;
    std::vector<unsigned int> indices;
    if constexpr((ThisMode & (uint32_t)gld::SceneLoadMode::HasGeometry) == (uint32_t)gld::SceneLoadMode::HasGeometry )
    {
        res->add_comp<gld::Render>(std::shared_ptr<gld::Render>(new gld::Render(vp,fp,std::get<0>(args))));
    }else{
        res->add_comp<gld::Render>(std::shared_ptr<gld::Render>(new gld::Render(vp,fp)));
    }
   
    res->add_comp<gld::Transform>(std::make_shared<gld::Transform>());

    for(unsigned int i = 0; i < mesh->mNumVertices; i++)
    {
        gld::def::Vertex vertex;
        vertex.pos = glm::vec3(mesh->mVertices[i].x,
            mesh->mVertices[i].y,
            mesh->mVertices[i].z);
        // normals
        vertex.normal = glm::vec3(mesh->mNormals[i].x,
            mesh->mNormals[i].y,
            mesh->mNormals[i].z);
        // texture coordinates
        if(mesh->mTextureCoords[0]) // does the mesh contain texture coordinates?
        {
            vertex.uv = glm::vec2(mesh->mTextureCoords[0][i].x,
                mesh->mTextureCoords[0][i].y);
        }
        else
            vertex.uv = glm::vec2(0.0f, 0.0f);
        vertices.push_back(vertex);
    }
    for(unsigned int i = 0; i < mesh->mNumFaces; i++)
    {
        aiFace face = mesh->mFaces[i];
        for(unsigned int j = 0; j < face.mNumIndices; j++)
            indices.push_back(face.mIndices[j]);
    }
    auto vao = std::make_shared<gld::VertexArr>();
    vao->create();
    vao->create_arr<gld::ArrayBufferType::VERTEX>();
    vao->create_arr<gld::ArrayBufferType::ELEMENT>();
    vao->bind();
    vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(vertices,GL_STATIC_DRAW);
    vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
        gld::VAP_DATA<3,float,false>,
        gld::VAP_DATA<3,float,false>,
        gld::VAP_DATA<2,float,false>>();
    vao->buffs().get<gld::ArrayBufferType::ELEMENT>().bind_data(indices,GL_STATIC_DRAW);
    vao->unbind();
    
    std::shared_ptr<gld::def::Mesh> mesh_comp = std::shared_ptr<gld::def::Mesh>(new gld::def::Mesh(indices.size(),vertices.size(),std::move(vao)));
    res->add_comp<gld::def::Mesh>(mesh_comp);
    if constexpr(((uint32_t)M & (uint32_t)gld::SceneLoadMode::NoMaterial) == 0x0 )
    {
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

        auto diffuse = get_material_tex(parent_path,material,aiTextureType_DIFFUSE,0);
        auto spec = get_material_tex(parent_path,material,aiTextureType_SPECULAR,0);

        res->add_comp<gld::def::Material>(std::shared_ptr<gld::def::Material>(
            new gld::def::Material(std::move(diffuse),std::move(spec))));

    }
    return res;
}

template<gld::SceneLoadMode M,typename ...Args>
std::shared_ptr<gld::Node<gld::Component>> process_node(aiNode *ai_node,const aiScene* scene,
    std::string& parent_path,std::string& vp,std::string& fp,std::tuple<Args...>& args,int deep = 0x7fffffff)
{
    auto node = std::shared_ptr<gld::Node<gld::Component>>(new gld::Node<gld::Component>());
    node->add_comp<gld::Transform>(std::make_shared<gld::Transform>());
    for(unsigned int i = 0;i < ai_node->mNumMeshes;++i)
    {
        node->add_child(process_mesh<M>(scene,scene->mMeshes[ ai_node->mMeshes[i] ],parent_path,vp,fp,args));
    }
    if(deep > 0)
    {
        for(unsigned int i = 0;i < ai_node->mNumChildren;++i)
        {
            node->add_child(process_node<M>(ai_node->mChildren[i],scene,parent_path,vp,fp,args,deep - 1));
        }
    }
    return node;
}
template<gld::SceneLoadMode M>
typename gld::LoadSceneNode<M>::RealRetTy gld::LoadSceneNode<M>::load(typename gld::LoadSceneNode<M>::ArgsTy args)
{
    auto [path,flag,vp,fp] = args;
    dbg::log << "LoadSceneNode @V@"_E << "path " << path << dbg::endl;
    auto ai = ResMgrWithGlslPreProcess::instance()->load<ResType::model>(std::string(path),flag);
    bool s = false;
    std::shared_ptr<Node<Component>> res;
    std::string parent_path = wws::get_parent(path);
    //dbg::log << "path " << path << "parent_path " << parent_path << dbg::endl;
    if(ai)
    {   
        std::tuple<> oths;
        if(auto node = process_node<M>(ai->GetScene()->mRootNode,ai->GetScene(),parent_path,vp,fp,oths);node)
        {
            return make_result(s,std::move(node));
        }
    }
    return make_result(s,std::move(res));
}

template<gld::SceneLoadMode M>
std::string gld::LoadSceneNodeWithGeom<M>::key_from_args(gld::LoadSceneNodeWithGeom<M>::ArgsTy args)
{
   return sundry::format_tup(args,'#');
}
template<gld::SceneLoadMode M>
std::string gld::LoadSceneNodeWithGeom<M>::key_from_args(std::tuple<const char*,unsigned int,const char*,const char*,const char *> args)
{
   return sundry::format_tup(args,'#');
}

template<gld::SceneLoadMode M>
typename gld::LoadSceneNodeWithGeom<M>::RealRetTy gld::LoadSceneNodeWithGeom<M>::load(std::tuple<const char*,unsigned int,const char*,const char*,const char *> args)
{
    auto [a,b,c,d,e] = args;
    return load(std::make_tuple(std::string(a),b,std::string(c),std::string(d),std::string(e)));
}

template<gld::SceneLoadMode M>
typename gld::LoadSceneNodeWithGeom<M>::RealRetTy gld::LoadSceneNodeWithGeom<M>::load(typename gld::LoadSceneNodeWithGeom<M>::ArgsTy args)
{
    auto [path,flag,vp,fp,gp] = args;
    dbg::log << "LoadSceneNode @V@"_E << "path " << path << dbg::endl;
    auto ai = ResMgrWithGlslPreProcess::instance()->load<ResType::model>(std::string(path),flag);
    bool s = false;
    std::shared_ptr<Node<Component>> res;
    std::string parent_path = wws::get_parent(path);
    //dbg::log << "path " << path << "parent_path " << parent_path << dbg::endl;
    if(ai)
    {   
        std::tuple<std::string> oths = std::make_tuple(std::move(gp));
        if(auto node = process_node<M>(ai->GetScene()->mRootNode,ai->GetScene(),parent_path,vp,fp,oths);node)
        {
            return make_result(s,std::move(node));
        }
    }
    return make_result(s,std::move(res));
}

#define InstanctationLoadSceneNode(LOAD_TY,TY)  \
template LOAD_TY<gld::SceneLoadMode::TY>;    \
template typename LOAD_TY<gld::SceneLoadMode::TY>::RealRetTy LOAD_TY<gld::SceneLoadMode::TY>::load(typename LOAD_TY<gld::SceneLoadMode::TY>::ArgsTy args);\
template std::string LOAD_TY<gld::SceneLoadMode::TY>::key_from_args(LOAD_TY<gld::SceneLoadMode::TY>::ArgsTy args);    \
template std::string LOAD_TY<gld::SceneLoadMode::TY>::key_from_args(std::tuple<const char*,unsigned int,const char*,const char*> args);  \
template typename LOAD_TY<gld::SceneLoadMode::TY>::RealRetTy LOAD_TY<gld::SceneLoadMode::TY>::load(std::tuple<const char*,unsigned int,const char*,const char*> args);    
  
#define InstanctationLoadSceneNodeWithGeom(LOAD_TY,TY)  \
template LOAD_TY<gld::SceneLoadMode::TY>;    \
template typename LOAD_TY<gld::SceneLoadMode::TY>::RealRetTy LOAD_TY<gld::SceneLoadMode::TY>::load(typename LOAD_TY<gld::SceneLoadMode::TY>::ArgsTy args);\
template std::string LOAD_TY<gld::SceneLoadMode::TY>::key_from_args(LOAD_TY<gld::SceneLoadMode::TY>::ArgsTy args);    \
template std::string LOAD_TY<gld::SceneLoadMode::TY>::key_from_args(std::tuple<const char*,unsigned int,const char*,const char*,const char*> args);  \
template typename LOAD_TY<gld::SceneLoadMode::TY>::RealRetTy LOAD_TY<gld::SceneLoadMode::TY>::load(std::tuple<const char*,unsigned int,const char*,const char*,const char*> args);    
  
#define InstanctationProcessMeshEx(TY,...)    \
template std::shared_ptr<gld::Node<gld::Component>> process_mesh<gld::SceneLoadMode::TY,##__VA_ARGS__>    \
    (const aiScene*,aiMesh*,std::string&,std::string&,std::string&,std::tuple<__VA_ARGS__>&);    \
template std::shared_ptr<gld::Node<gld::Component>> process_node<gld::SceneLoadMode::TY,##__VA_ARGS__>(aiNode *,const aiScene* ,  \
    std::string& ,std::string& ,std::string& ,std::tuple<__VA_ARGS__>&,int );

InstanctationLoadSceneNode(gld::LoadSceneNode,Default)
InstanctationLoadSceneNode(gld::LoadSceneNode,NoMaterial)

InstanctationLoadSceneNodeWithGeom(gld::LoadSceneNodeWithGeom,HasGeometry)
InstanctationLoadSceneNodeWithGeom(gld::LoadSceneNodeWithGeom,HasGeometry_NoMaterial)

InstanctationProcessMeshEx(Default)
InstanctationProcessMeshEx(NoMaterial)

InstanctationProcessMeshEx(HasGeometry,std::string)

InstanctationProcessMeshEx(HasGeometry_NoMaterial,std::string)