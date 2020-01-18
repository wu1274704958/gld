#pragma once
#include "gl_comm.hpp"
#include <glad/glad.h>
#include <array>
#include <tuple>
#include <unordered_map>


namespace gld{
    
    template<ShaderType ST>
    class Shader{
    public:
        constexpr static size_t ShaderTy = static_cast<size_t>(ST);
        Shader():id(0){}
        Shader(Glid id):id(id){}
        Shader(const Shader<ST>& oth)=delete;
        Shader(Shader<ST> &&oth)
        {
            id = oth.id;
            oth.id = 0;
        }
        Shader<ST>& operator=(const Shader<ST>&) = delete;
        Shader<ST>& operator=(Shader<ST>&& oth)
        {
            clean();
            id = oth.id;
            oth.id = 0;
			return *this;
        }
        void clean(){
            if(id > 0)
                glDeleteShader(id);
            id = 0;
        }
        ~Shader(){
            clean();
        }
        Glid get_id()
        {
            return id;
        }
        operator Glid*()
        {
            return &id;
        }
    private:
        Glid id;
    };

    

    class Shaders {
    public:
        using MapType = IdxArr<static_cast<size_t>(ShaderType::VERTEX),static_cast<size_t>(ShaderType::FRAGMENT),static_cast<size_t>(ShaderType::GEOMETRY)>;

        template<ShaderType ST>
        void set(Shader<ST> v){
            std::get<get_idx<static_cast<size_t>(ST)>(MapType())>(tup) = std::move(v);
        }

        template<ShaderType ST>
        Shader<ST>& get(){
            return std::get<get_idx<static_cast<size_t>(ST)>(MapType())>(tup);
        }
		Shaders() {}
		Shaders(const Shaders&) = delete;
		Shaders(Shaders&& s) {
			tup = std::move(tup);
		}

		Shaders& operator=(const Shaders&) = delete;
		Shaders& operator=(Shaders&& s) {
			tup = std::move(tup);
			return *this;
		}

        private:
        std::tuple<Shader<ShaderType::VERTEX>,Shader<ShaderType::FRAGMENT>,Shader<ShaderType::GEOMETRY>> tup;
    };

    class Program{
    public:
		void cretate() {
			program = glCreateProgram();
		}
        void use(){
			glUseProgram(program);
        }

        template<ShaderType ST>
        void attach_shader(Shader<ST> s)
        {
            shaders.set(std::move(s));
            glAttachShader(program,shaders.get<ST>().get_id());
        }

        void link()
        {
            glLinkProgram(program);
        }

		bool good() {
			return program > 0;
		}

		void clean() {
			if (good())
				glDeleteProgram(program);
			program = 0;
		}
		Program() {}
		Program(const Program& oth) = delete;
		Program(Program&& oth)
		{
			program = oth.program;
			oth.program = 0;
            uniform_map = std::move(uniform_map);
			shaders = std::move(shaders);
		}
		Program& operator=(const Program&) = delete;
		Program& operator=(Program&& oth)
		{
			clean();
			program = oth.program;
			oth.program = 0;

            uniform_map = std::move(uniform_map);
			shaders = std::move(shaders);
			return *this;
		}
		~Program() {
			clean();
		}

		operator Glid ()
		{
			return program;
		}

        const Shaders& get_shaders() const
        {
            return shaders;
        }
        template<typename ...Args>
        void locat_uniforms(Args&& ...args)
        {
            if constexpr(sizeof...(Args) > 0)
            {
                locat_uniforms_inside(std::forward<Args>(args)...);
            }
        }

        Glid uniform_id(std::string str)
        {
            if(uniform_map.find(str) != uniform_map.end())
            {
                return uniform_map[str];
            }else
                return 0;
        }
    private:

        template<typename Fir,typename ...Args>
        void locat_uniforms_inside(Fir&& fir,Args&& ...args)
        {
            if constexpr(std::is_same_v<Fir,std::string>)
            {
                uniform_map[std::forward<Fir>(fir)] = glGetUniformLocation(program,fir.c_str()); 
            }else if constexpr(std::is_same_v<std::decay_t<Fir>,const char *>)
            {
                uniform_map[std::string(fir)] = glGetUniformLocation(program,fir); 
            }
            if constexpr(sizeof...(Args) > 0)
            {
                locat_uniforms_inside(std::forward<Args>(args)...);
            }
        }

        Shaders shaders;
        Glid program = 0;
        std::unordered_map<std::string,Glid> uniform_map;
    }; 
}