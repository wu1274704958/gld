#include <component.h>
#include <comps/sundry.h>
namespace gld{
    bool Render::init()  
    {
        if(geom_path)
        {
            program = DefDataMgr::instance()->load<DataType::ProgramWithGeometry>(vert_path,frag_path,*geom_path);
        }else{
            program = DefDataMgr::instance()->load<DataType::Program>(vert_path,frag_path);
        }
        return (bool)program;
    }
    void Render::before_draw() 
    {
        program->use();
    }
    void Render::after_draw() 
    {
        program->unuse();
    }
    int64_t Render::idx() { return -100;}
    std::shared_ptr<Program> Render::get()
    {
        return program;
    }


    Transform::Transform(std::string model_key) : model(std::move(model_key))
    {
        rotate = pos = glm::vec3(0.f, 0.f, 0.f);
        scale = glm::vec3(1.f, 1.f, 1.f);
    }

    Transform::Transform() : model("model")
    {
        rotate = pos = glm::vec3(0.f, 0.f, 0.f);
        scale = glm::vec3(1.f, 1.f, 1.f);
    }

    bool Transform::init() 
    {
        auto n_ptr = get_node();
        auto render = n_ptr->get_comp<Render>();
        if(render)
            model.attach_program(render->get());
        else
            no_render = true;
        return true;
    }

    void Transform::draw()
    {
        if(!no_render)
        {
            glm::mat4 mat = get_model();

            update_matrix(mat);
        }
    }

    glm::mat4 Transform::get_model()
    {
        glm::mat4 mat(1.0f);

        mat = glm::translate(mat, pos);

        mat = glm::scale(mat, scale);

        mat = glm::rotate(mat, rotate.x, glm::vec3(1.f, 0.f, 0.f));
        mat = glm::rotate(mat, rotate.y, glm::vec3(0.f, 1.f, 0.f));
        mat = glm::rotate(mat, rotate.z, glm::vec3(0.f, 0.f, 1.f));

        auto n_ptr = get_node();
        if(n_ptr && n_ptr->has_parent())
        {
            mat = n_ptr->get_parent()->get_comp<Transform>()->get_model() * mat;
        }

        return mat;
    }

    void Transform::update_matrix(glm::mat4 mat)
    {
        model = glm::value_ptr(mat);
    }

    void Transform::setRotateX(float x) {
        rotate.x = glm::radians(x);
    }
    void Transform::setRotateY(float x) {
        rotate.y = glm::radians(x);
    }
    void Transform::setRotateZ(float x) {
        rotate.z = glm::radians(x);
    }
    void Transform::setRotation(glm::vec3 r) {
        rotate.x = glm::radians(r.x);
        rotate.y = glm::radians(r.y);
        rotate.z = glm::radians(r.z);
    }


    LocalTransForm::LocalTransForm(std::string local_key) : local_mat(std::move(local_key))
    {
        rotate = pos = glm::vec3(0.f, 0.f, 0.f);
        scale = glm::vec3(1.f, 1.f, 1.f);
    }

    LocalTransForm::LocalTransForm() : local_mat("local_mat")
    {
        rotate = pos = glm::vec3(0.f, 0.f, 0.f);
        scale = glm::vec3(1.f, 1.f, 1.f);
    }

    bool LocalTransForm::init()
    {
        auto n_ptr = get_node();
        auto render = n_ptr->get_comp<Render>();
        if (render)
            local_mat.attach_program(render->get());
        return true;
    }

    void LocalTransForm::draw()
    {
        glm::mat4 mat = get_model();
        update_matrix(mat);
    }

    glm::mat4 LocalTransForm::get_model()
    {
        glm::mat4 mat(1.0f);

        mat = glm::translate(mat, pos);

        mat = glm::scale(mat, scale);

        mat = glm::rotate(mat, rotate.x, glm::vec3(1.f, 0.f, 0.f));
        mat = glm::rotate(mat, rotate.y, glm::vec3(0.f, 1.f, 0.f));
        mat = glm::rotate(mat, rotate.z, glm::vec3(0.f, 0.f, 1.f));

        return mat;
    }

    void LocalTransForm::update_matrix(glm::mat4 mat)
    {
        local_mat = glm::value_ptr(mat);
    }

    void LocalTransForm::setRotateX(float x) {
        rotate.x = glm::radians(x);
    }
    void LocalTransForm::setRotateY(float x) {
        rotate.y = glm::radians(x);
    }
    void LocalTransForm::setRotateZ(float x) {
        rotate.z = glm::radians(x);
    }
    void LocalTransForm::setRotation(glm::vec3 r) {
        rotate.x = glm::radians(r.x);
        rotate.y = glm::radians(r.y);
        rotate.z = glm::radians(r.z);
    }
}