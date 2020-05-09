#define _CRT_SECURE_NO_WARNINGS
#include <resource_mgr.hpp>
#include <FindPath.hpp>
#include <glad/glad.h>
#include <RenderDemoRotate.hpp>
#include <cstdio>
#include <memory>
#include <macro.hpp>
#include <sundry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <dbg.hpp>
#include <vector>
#include <make_color.hpp>
#include <gl_comm.hpp>
#include <drawable.h>
#include <program.hpp>
#include <vertex_arr.hpp>
#include <uniform.hpp>
#include <texture.hpp>
#include <comm.hpp>
#include <random>
#include <uniform_buf.hpp>
#include <log.hpp>
#include <assimp/scene.h>
#include <data_mgr.hpp>
#include <component.h>
#include <comps/Material.hpp>
#include <frame_rate.h>
#include <ani_surface.hpp>
#include <surface.hpp>
#include <strstream>
#include <make_color.hpp>
#include <frame_buffer.hpp>
#include <generator/Generator.hpp>
#include "../lighting_6/model.hpp"
#include "../lighting_6/light.hpp"
#include <spy.hpp>

using namespace gld;
namespace fs = std::filesystem;

using  namespace dbg::literal;

using namespace wws;
using namespace ft2;

#define MODE_4

float rd_0_1()
{
    std::random_device r;
    std::default_random_engine e1(r());
    std::uniform_int_distribution<int> uniform_dist(0, 1000000);
    return static_cast<float>(uniform_dist(e1)) / 100000.f;
}
glm::vec3 rd_vec3()
{
    return glm::vec3(rd_0_1(),rd_0_1(),rd_0_1());
}

class GLContent {
	public:
		glm::vec3* ptr = nullptr;
		int w;
		int h;
	
		using PIXEL_TYPE = glm::vec3;
		using PRESENT_ARGS_TYPE = std::function<void(const GLContent*)>;
		constexpr static PIXEL_TYPE PIXEL_ZERO = glm::vec3(0.f,0.f,0.f);
		constexpr static PIXEL_TYPE EMPTY_PIXEL = glm::vec3(-1.f,-1.f,-1.f);
		GLContent(int w, int h) {
			this->w = w;
			this->h = h;
			ptr = new glm::vec3[this->w * this->h];
		}
		~GLContent() {
			if (ptr)
			{
				delete[] ptr;
			}
		}
		GLContent(const GLContent&) = delete;
		GLContent(GLContent&& oth)
		{
			ptr = oth.ptr;
			oth.ptr = nullptr;
		}
		GLContent& operator=(const GLContent&) = delete;
		GLContent& operator=(GLContent&& oth)
		{
			if (ptr)
			{
				delete[] ptr;
				ptr = nullptr;
			}
			ptr = oth.ptr;
			oth.ptr = nullptr;
			return *this;
		}

		virtual void init()
		{
			for (int i = 0; i < w * h; ++i)
			{
				ptr[i] = EMPTY_PIXEL;
			}
		}

		virtual void set_pixel(int x, int y, PIXEL_TYPE p)
		{
			ptr[(y * w) + x] = p;
		}

		virtual PIXEL_TYPE get_pixel(int x, int y) const
		{
			return ptr[(y * w) + x];
		}

		virtual void swap(GLContent& oth)
		{
			auto temp = oth.ptr;
			oth.ptr = this->ptr;
			this->ptr = temp;
		}

		virtual void present(PRESENT_ARGS_TYPE a) const
		{
			a(this);
		}

		void clear()
		{
			init();
		}

	};


void set_text(surface<GLContent>& sur, Face& f, std::string s,glm::vec3 pt);

struct Drive : public ASDrive<GLContent>
{
	Drive(Face& f,std::function<void(const GLContent*)> func) : face(f),func(func)
	{
		str = get_time_str(has_p);
	}

	std::string get_time_str(bool has_p)
	{
		std::strstream ss; 
		std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
		
    	auto now_c = std::chrono::system_clock::to_time_t(now);
        auto t = std::localtime(&now_c);
		ss <<  
			(t->tm_hour < 10 ? "0" : "") << 
			t->tm_hour << (has_p ? ':' : ' ') << 
			(t->tm_min < 10 ? "0" : "") << 
			t->tm_min << ':' << 
			(t->tm_sec < 10 ? "0" : "") << 
			t->tm_sec << '\0';
		return ss.str();
	}

	bool is_end()
	{
		return false;
	}
	void set_text(surface<GLContent>& sur,glm::vec3 f_c) override
	{
		{
			::set_text(sur,face,str,f_c);
		}
	}
	void step() override
	{
		str = get_time_str(has_p);
	}
	std::function<void(const GLContent*)> get_present() override
	{
		return func;
	}
	bool need_transfar(uint32_t ms,bool to_use_stable,bool to_out_stable) override 
	{
		bool f = to_use_stable && ms >= 1000;
		if(f)
		{
			has_p = !has_p;
		}
		return f;
	}
	bool has_p = true;
	std::string str;
	Face& face;
    std::function<void(const GLContent*)> func;
};

struct PointMaterial:public Component
{
    PointMaterial( std::shared_ptr<Texture<TexType::D2>> diffuseTex):
        udiffuseTex("diffuseTex"),
        //uColor("fill_color"),
        diffuseTex(std::move(diffuseTex))
    {
        udiffuseTex = 0;
        //uColor = glm::vec3(1.f,1.f,1.f);
    }
    bool init() override
    {
        bool f = true;
        if(diffuseTex)
        {
            diffuseTex->bind();
            diffuseTex->generate_mipmap();
            diffuseTex->set_paramter<TexOption::WRAP_S,TexOpVal::CLAMP_TO_EDGE>();
            diffuseTex->set_paramter<TexOption::WRAP_T,TexOpVal::CLAMP_TO_EDGE>();
            diffuseTex->set_paramter<TexOption::MIN_FILTER,TexOpVal::LINEAR_MIPMAP_LINEAR>();
            diffuseTex->set_paramter<TexOption::MAG_FILTER,TexOpVal::LINEAR>();
            f = false;
        }
        auto n_ptr = get_node();
        auto render = n_ptr->get_comp<Render>();
        udiffuseTex.attach_program(render->get());
        //uColor.attach_program(render->get());
        return f;
    }
    void draw() override
    {
        if(diffuseTex)
            diffuseTex->active<ActiveTexId::_0>();
        //uColor.sync();
        udiffuseTex.sync();
    }
    void after_draw() override
    {
        diffuseTex->unbind();
    }
   
    GlmUniform<UT::Sampler2D>   udiffuseTex;
    //GlmUniform<UT::Vec3>        uColor;
    std::shared_ptr<Texture<TexType::D2>> diffuseTex;
};

struct Point{
    glm::mat4 model;
    glm::vec3 color;

    Point() : model(1.f), color(1.f,1.f,1.f){}
};

struct AutoRotate : public Component
{
    bool add = true;
    void update() override
    {
        auto trans = node.lock()->get_comp<Transform>();
        if(trans->rotate.y >= 0.86f)
            add = false;
        else
        if(trans->rotate.y <= -0.86f)
            add = true;
        trans->rotate.y += (add ?  0.0001f : -0.0001f) * FrameRate::get_ms();
    }
};

class Demo1 : public RenderDemoRotate {
public:
    Demo1() : perspective("perspective"), world("world") ,view_pos("view_pos")
        {}
    int init() override
    {
        RenderDemoRotate::init();
        
        program = DefDataMgr::instance()->load<DataType::Program>("point/base_vs.glsl","point/base_fg.glsl");
        program->use();

        program->locat_uniforms("perspective", "world", "model", "diffuseTex");

        perspective.attach_program(program);
        world.attach_program(program);

        //curve surface pargram +++
        curve_p = DefDataMgr::instance()->load<DataType::Program>("base/curve_vs.glsl","base/curve_fg.glsl");
        curve_p->use();

        curve_p->locat_uniforms("perspective", "world", "model", "diffuseTex", "ambient_strength",
            "specular_strength",
            "view_pos",
            "shininess","specularTex");

        view_pos.attach_program(curve_p);
        
        glm::vec3 vp(0.f,0.f,0.f );
        view_pos = glm::value_ptr(vp);

        light.init(GL_STATIC_DRAW);
        
        light->color = glm::vec3(1.f, 1.f, 1.f);
        light->dir = glm::vec3(-0.2f, -1.0f, -0.3f);

        light.sync(GL_MAP_WRITE_BIT| GL_MAP_INVALIDATE_BUFFER_BIT);
        //curve surface pargram ---

        program->use();

        float planeVertices[] = {
            // positions          //normal            // texture Coords 
             0.5f, 0.5f, 0.f,   0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
            -0.5f, 0.5f, 0.f,   0.0f,  1.0f,  0.0f,  0.0f, 0.0f,
            -0.5f,-0.5f, 0.f,   0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
  
             0.5f, 0.5f, 0.f,   0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
            -0.5f,-0.5f, 0.f,   0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
             0.5f,-0.5f, 0.f,   0.0f,  1.0f,  0.0f,  1.0f, 1.0f
        };
#ifndef PF_ANDROID
        glEnable(GL_FRAMEBUFFER_SRGB);
#endif

        screen_tex = std::make_shared<Texture<TexType::D2>>();
        screen_tex->create();
        screen_tex->bind();
        screen_tex->tex_image(0,GL_RGB,0,GL_RGB,(unsigned char *)nullptr,(int)screen_w,(int)screen_h);
        screen_tex->set_paramter<TexOption::MIN_FILTER,TexOpVal::LINEAR>();
        screen_tex->set_paramter<TexOption::MAG_FILTER,TexOpVal::LINEAR>();
        screen_tex->set_paramter<TexOption::WRAP_R,TexOpVal::CLAMP_TO_EDGE>();
        screen_tex->set_paramter<TexOption::WRAP_S,TexOpVal::CLAMP_TO_EDGE>();
        screen_tex->generate_mipmap();


        fb.create();
        fb.bind();
        fb.attach_texture(*screen_tex.get(),GL_COLOR_ATTACHMENT0,0);
        
        rb.create();
        rb.bind();
        rb.storage(GL_DEPTH24_STENCIL8,(int)screen_w,(int)screen_h);
        
        fb.attach_buffer(GL_DEPTH_STENCIL_ATTACHMENT,rb);

        dbg(fb.check_status() == GL_FRAMEBUFFER_COMPLETE);

        fb.unbind();

        //curved_surface node init +++++

        auto mesh = gen::curved_surface(0.6f,0.8f,0.2f,0.16f,64);

        mesh->mode = GL_TRIANGLE_STRIP;

        curve_sur = std::make_shared<Node<Component>>();
        curve_sur->add_comp<Transform>(std::make_shared<Transform>());
        curve_sur->add_comp<def::Mesh>(mesh);
        curve_sur->add_comp<Render>(std::make_shared<Render>("base/curve_vs.glsl","base/curve_fg.glsl"));

        
        dif_plane = DefDataMgr::instance()->load<DataType::Texture2D>("textures/circle.png",0);
        auto spec_plane = DefDataMgr::instance()->load<DataType::Texture2D>("textures/love.png",0);

        auto plane_mat = std::shared_ptr<def::Material>(new def::Material(screen_tex,spec_plane));
        plane_mat->uambient_strength = 0.0001f;
        plane_mat->ushininess = 256.f;
        plane_mat->uspecular_strength = 0.5f;

        curve_sur->add_comp<def::Material>(plane_mat);

        curve_sur->get_comp<Transform>()->scale = glm::vec3(5.6f,4.f,4.f);

        //curved_surface node init ----------

        std::function<void(const GLContent*)> update = [=](const GLContent* c)
        {
            std::vector<Point> ps;

            for(int y = 0;y < ph;++y)
            {
                for(int x = 0;x < pw;++x)
                {   
                    #ifdef MODE_4
                        int z_k = y * pw + x;
                    #endif
                    auto color = c->get_pixel(x,y);
                    if(color != GLContent::EMPTY_PIXEL)
                    #ifdef MODE_1
                            ps.push_back(create_point(glm::vec3((float)x + this->bx,(float)y + this->by,rd_0_1() - 0.5f),color));
                    #endif
                    #ifdef MODE_0
                            ps.push_back(create_point(glm::vec3((float)x + this->bx,(float)y + this->by,0.0f),color));
                    #endif
                    #ifdef MODE_2
                            ps.push_back(create_point(glm::vec3((float)x + this->bx,(float)y + this->by,z_arr[(y * pw) + x]),color));
                    #endif
                    #ifdef MODE_3
                        ps.push_back(create_point(glm::vec3((float)x + this->bx,(float)y + this->by,0.0f),color));
                    else
                    {
                        ps.push_back(create_point(glm::vec3((float)x + this->bx,(float)y + this->by,0.0f),glm::vec3(0.46f)));
                    }
                    #endif
                    #ifdef MODE_4
                    {
                        z_arr[z_k] = 1.0f;
                    }else{
                        z_arr[z_k] *= 0.9f;
                        color = this->light_color;
                    }
                    float offset = (y % 2) == 0 ? -0.3f : 0.3f;
                    ps.push_back(create_point(glm::vec3((float)x + this->bx + offset,(float)y + this->by,0.0f),color * z_arr[z_k]));
                    #endif
                }
            }
            sync_vertex_data(ps);
            
        };
#ifdef MODE_2
        z_arr = std::unique_ptr<float[]>(new float[pw * ph]);

        for(int i = 0;i < pw * ph;++i)
            z_arr[i] = rd_0_1() * 0.2f;
#endif

#ifdef MODE_4
        z_arr = std::unique_ptr<float[]>(new float[pw * ph]);

        for(int i = 0;i < pw * ph;++i)
            z_arr[i] = 0.f;
#endif

	    try
	    {
            auto f = DefResMgr::instance()->load<ResType::font>("fonts/SHOWG.TTF");

	    	face = lib.load_face_for_mem<Face>(f->get_data(),f->get_size(),0);
	    }
	    catch (const std::exception& e)
	    {
	    	std::cout << e.what() << std::endl;
	    	return -1;
	    }
    

	    srand(static_cast<unsigned int>(time(nullptr)));
	    face.set_pixel_size(56, 56);
	    face.select_charmap(FT_ENCODING_UNICODE);

	    drive = std::make_shared<Drive>(face,update);

        light_color = wws::make_rgb(PREPARE_STRING("00F5FF")).make<glm::vec3>();

	    sur = std::shared_ptr<AniSurface<GLContent>>(new AniSurface(224,56,drive.get(),light_color,300));
	    sur->to_out_speed = 0.09f;
	    sur->to_use_speed = 0.1f;

	    sur->move_to_func = [](cgm::vec2& pos,cgm::vec2 v,cgm::vec2 tar)
	    {
	    	auto len = (tar - pos).len() * 0.1f;
	    	pos = pos + ( v * len);
	    };

	    sur->set_min_frame_ms(16);

        auto plane_vao = std::make_shared<gld::VertexArr>();
        plane_vao->create();
        plane_vao->create_arr<gld::ArrayBufferType::VERTEX>();
        plane_vao->bind();
        plane_vao->buffs().get<gld::ArrayBufferType::VERTEX>().bind_data(planeVertices,GL_STATIC_DRAW);
        plane_vao->buffs().get<gld::ArrayBufferType::VERTEX>().vertex_attrib_pointer<
         gld::VAP_DATA<3,float,false>,
         gld::VAP_DATA<3,float,false>,
         gld::VAP_DATA<2,float,false>>();
        plane_vao->unbind();

        plane_mesh = std::shared_ptr<def::MeshInstanced>(new def::MeshInstanced(0,wws::arrLen(planeVertices)/8, pw * ph ,plane_vao ));

        

        bx = -static_cast<float>(pw) / 2.f; 
        by = -static_cast<float>(ph) / 2.f;

        cxts.push_back(create_point());
        cxts[0]->get_comp<Transform>()->setRotateX(180.f);
        
        // glEnable(GL_BLEND);
        // glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

        glDisable(GL_CULL_FACE);
        //glCullFace(GL_BACK);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        GLenum err = glGetError();
        dbg(err);

        for (auto& p : cxts)
            p->init();
        
        curve_sur->init();

        sur->pre_go();

        return 0;
    }

    std::shared_ptr<Node<Component>> create_point(glm::vec3 pos = glm::vec3(0.f,0.f,0.f))
    {
        std::shared_ptr<Node<Component>> res = std::make_shared<Node<Component>>();
        res->add_comp<Transform>(std::make_shared<Transform>());
        res->get_comp<Transform>()->pos = pos;
        res->add_comp(plane_mesh);
        res->add_comp<Render>(std::shared_ptr<Render>(new Render("point/base_vs.glsl","point/base_fg.glsl")));
        res->add_comp<PointMaterial>(std::shared_ptr<PointMaterial>(new PointMaterial(dif_plane)));
        // #ifdef MODE_1
        // res->add_comp<AutoRotate>(std::make_shared<AutoRotate>());
        // #endif
        return res;
    }

    Point create_point(glm::vec3 pos,glm::vec3 color)
    {
        Point res;
        res.color = color;
        res.model = glm::translate(res.model,pos);
        return res;
    }

    void sync_vertex_data(std::vector<Point>& ps)
    {
        auto& vao = plane_mesh->vao;
        vao->bind_self();

        VABuffer<ArrayBufferType::VERTEX>* buf;

        if(vao->get_oths().empty())
            buf = &(vao->create_one());
        else
        {
            buf = &(vao->get_oths()[0]);
            buf->bind();
        }
            
        buf->bind_data(ps.data(), ps.size() ,GL_STATIC_DRAW);
        
        buf->vertex_attrib_pointer<3,
            VAP_DATA<4,float,false>,
            VAP_DATA<4,float,false>,
            VAP_DATA<4,float,false>,
            VAP_DATA<4,float,false>,
            VAP_DATA<3,float,false>
            >();

        vao->vertex_attr_div<3,1,1,1,1,1>();
        vao->unbind_self();
        plane_mesh->instance_count = ps.size();
    }

    std::vector<Point> verex_data_surface()
    {
        std::vector<Point> ps;
        auto c = rd_vec3();

        for(int y = 0;y < ph;++y)
        {
            for(int x = 0;x < pw;++x)
            {
                auto c = rd_vec3();
                ps.push_back(create_point(glm::vec3((float)x + bx,(float)y + by,0.0f),c));
            }
        }
        return ps;
    }

    void draw() override
    {
        fb.bind();
        glViewport(0, 0, (GLsizei)screen_w, (GLsizei)screen_h);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        program->use();
        update_matrix_clock();
        perspective.attach_program(program);
        world.attach_program(program);
      
        perspective.sync();
        world.sync();

        for (auto& p : cxts)
            p->draw();

        fb.unbind();
        // draw curve_surface 
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        curve_p->use();
        update_matrix();
        perspective.attach_program(curve_p);
        world.attach_program(curve_p);

        perspective.sync();
        world.sync();

        curve_sur->draw();

        update();
    }

    void update_matrix()
    {
        perspective = glm::perspective(glm::radians(60.f), ((float)width / (float)height), 0.1f, 256.0f);
        world = glm::mat4(1.0f);

        world = glm::translate(*world, glm::vec3(0.f,0.f, -4.0f));
        world = glm::rotate(*world, glm::radians(rotate.x), glm::vec3(1.f, 0.f, 0.f));
        world = glm::rotate(*world, glm::radians(rotate.y), glm::vec3(0.f, 1.f, 0.f));
        world = glm::rotate(*world, glm::radians(rotate.z), glm::vec3(0.f, 0.f, 1.f));
    }

    void update_matrix_clock()
    {
        perspective = glm::perspective(glm::radians(60.f), screen_w / screen_h, 0.1f, 256.0f);
        world = glm::mat4(1.0f);

        world = glm::translate(*world, glm::vec3(0.f,0.f, -103.0f));
    }

    void update()
    {
        for (auto& p : cxts)
            p->update();

        curve_sur->update();

        if(!sur->is_end())
            sur->ani_step();
    }

    ~Demo1() {

    }

    void onMouseButton(int btn,int action,int mode) override
    {
        RenderDemoRotate::onMouseButton(btn,action,mode);
        if(btn == GLFW_MOUSE_BUTTON_2 && action == GLFW_PRESS)
        {
            auto ps = verex_data_surface();

            sync_vertex_data(ps);
        }
    }

    void onWindowResize(int w, int h) override
    {
        glViewport(0, 0, w, h);
    }
private:
    std::shared_ptr<Program> program,curve_p;
    GlmUniform<UT::Matrix4> perspective;
    GlmUniform<UT::Matrix4> world;
    std::vector<std::shared_ptr< gld::Node<gld::Component>>> cxts;
    std::shared_ptr<def::MeshInstanced> plane_mesh;
    //std::shared_ptr<Material> plane_mat;
    std::shared_ptr<Texture<TexType::D2>> dif_plane;
    std::shared_ptr<Render> render;
    int pw = 216,ph = 56;
    float bx,by;
    Library lib;
    Face face;
    std::shared_ptr<AniSurface<GLContent>> sur;
    std::shared_ptr<Drive> drive;
    #ifdef MODE_2
    std::unique_ptr<float[]> z_arr;
    #endif
    #ifdef MODE_4
    std::unique_ptr<float[]> z_arr;
    #endif
    glm::vec3 light_color;
    std::shared_ptr<Texture<TexType::D2>> screen_tex;
    FrameBuffer fb;
    RenderBuffer rb;
    float screen_w = 860.f * 2.f,screen_h = 460.f * 2.f;
    UniformBuf<0,DictLight> light;
    Uniform<UT::Vec3> view_pos;
    std::shared_ptr<gld::Node<gld::Component>> curve_sur;
};

#ifndef PF_ANDROID
int main()
{
    fs::path root = wws::find_path(3, "res", true);
    ResMgrWithGlslPreProcess::create_instance(root);
    DefResMgr::create_instance(std::move(root));
    Demo1 d;
    
    if (d.initWindow(1920, 1060, "Clock",[](){
        glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
        glfwWindowHint(GLFW_DECORATED, GL_FALSE); 
    }))
    {
        printf("init window failed\n");
        return -1;
    }
     

    auto sp = spy::Spy::EnumWindowsByTitleAndCls("Program Manager","Progman");
    auto self = ::GetActiveWindow();
    if(!sp.get_windows().empty())
    {
        auto root = sp.get_windows()[0].get_hwnd();
        dbg(root);

        auto def = ::FindWindowExA(root,NULL,"SHELLDLL_DefView",NULL);
        
        //::SetParent(self,root);
        if(def)
        {
            ::SetParent(def,root);
            // auto lv = ::FindWindowExA(def,NULL,"SysListView32","FolderView");
            // if(lv)
            // {
            //     dbg(lv);
            //     auto self = ::GetActiveWindow();
            //     ::SetParent(self,lv);
            // }
        }
    }


    d.init();
    d.run();

    return 0;
}

#endif


static int colon_width = 7;

void set_text(surface<GLContent>& sur, Face& f, std::string s,glm::vec3 pt)
{
	int x = 0;
	for (auto c : s)
	{
		if(c == ' ')
		{
			x += colon_width;
		}else
		{
			f.load_glyph(c);
			CenterOff custom;
			if(c == ':')
			{
				colon_width = f.render_surface(sur,custom, &surface<GLContent>::set_pixel, x, 0, pt);
				x += colon_width;
			}
			else
			{
				x += f.render_surface(sur,custom, &surface<GLContent>::set_pixel, x, 0, pt);
			}
		}
	}
}