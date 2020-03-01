#include <component.h>

namespace gld{
    bool Render::init()  
    {
        program = DefDataMgr::instance()->load<DataType::Program>(vert_path,frag_path);
        return (bool)program;
    }
    void Render::on_draw() 
    {
        program->use();
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

    bool Transform::init() 
    {
        auto render = get_node()->get_comp<Render>();
        model.attach_program(render->get());
        return true;
    }

    void Transform::on_draw()
    {
        glm::mat4 mat = get_model();

        auto n_ptr = get_node();
        if(n_ptr && n_ptr->has_parent())
        {
            mat = n_ptr->get_parent()->get_comp<Transform>()->get_model() * mat;
        }

        update_matrix(mat);
    }

    glm::mat4 Transform::get_model()
    {
        glm::mat4 mat(1.0f);

        mat = glm::translate(mat, pos);

        mat = glm::scale(mat, scale);

        mat = glm::rotate(mat, rotate.x, glm::vec3(1.f, 0.f, 0.f));
        mat = glm::rotate(mat, rotate.y, glm::vec3(0.f, 1.f, 0.f));
        mat = glm::rotate(mat, rotate.z, glm::vec3(0.f, 0.f, 1.f));

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
}