#pragma once
#include <component.h>
 
namespace gld {
	struct LocalTransForm : Component
	{
        LocalTransForm(std::string local_key);
        LocalTransForm();

        bool init() override;

        void draw() override;
        glm::mat4 get_model();
        virtual void update_matrix(glm::mat4 mat);

        void setRotateX(float x);
        void setRotateY(float x);
        void setRotateZ(float x);
        void setRotation(glm::vec3 r);

        glm::vec3 pos;
        glm::vec3 rotate;
        glm::vec3 scale;
    protected:
        Uniform<UT::Matrix4> local_mat;
	};
}