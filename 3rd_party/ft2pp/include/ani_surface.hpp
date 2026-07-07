#pragma once
#include "ft2pp.hpp"
#include <memory>
#include <vector>
#include <matrix2.hpp>
#include "comm_in.hpp"
#include <functional>
#include <surface.hpp>
#include <thread>


namespace wws{

    

    struct point {
	    cgm::vec2 pos;
	    cgm::vec2 v;
	    cgm::vec2 tar;
		float distance = 0.0f;
	    point() : pos({ -1.f,-1.f }) {
            
	    }
    };

	template<typename Cnt>
    struct ASDrive
    {
        virtual bool is_end() = 0;
        virtual void set_text(surface<Cnt>& sur,typename Cnt::PIXEL_TYPE pt) = 0;
        virtual void step() = 0;
		virtual bool need_transfar(uint32_t ms,bool to_use_stable,bool to_out_stable) = 0;
		virtual typename Cnt::PRESENT_ARGS_TYPE get_present() = 0;
        virtual ~ASDrive(){}
    };

	template<typename Cnt>
    struct AniSurface
    {
        AniSurface(int w,int h,ASDrive<Cnt>* drive,typename Cnt::PIXEL_TYPE pt,int reserve = 100) : 
            sur( w, h),
            last(w, h),
            back(w, h),
            drive(drive),
			fill_byte(pt)
        {
            use.reserve(reserve);
	        out.reserve(reserve);
        }
        std::unique_ptr<point>& get_out_to_use()
	    {
	    	if (out.empty())
	    	{
	    		use.push_back(std::unique_ptr<point>(new point()));
	    		return use.back();
	    	}
	    	use.push_back(std::move(out.back()));
	    	out.pop_back();
	    	return use.back();
	    }

	    std::unique_ptr<point>& get_use_to_out (int x,int y)
	    {
	    	auto it = use.end();
	    	for (it = use.begin(); it != use.end(); ++it)
	    	{
	    		auto& p = *it;
	    		if ((p->pos.x() == x && p->pos.y() == y) || (p->tar.x() == x && p->tar.y() == y))
	    		{
	    			break;
	    		}
	    	}
	    	if(it != use.end())
	    	{
	    		out.push_back(std::move(*it));
	    		use.erase(it);
	    		return out.back();
	    	}
	    	else {
	    		throw std::runtime_error("Not found!");
	    	}
	    }

	    cgm::vec2 rd_out_pos (int x,int y){
		    if (x < out_MaxW || (sur.w() - x) < out_MaxW )
		    {
		    	int res_x = x < out_MaxW ? -1 : sur.w();
		    	int off = rand() % out_M;
		    	if (y + off < sur.h())
		    		return cgm::vec2{ static_cast<float>(res_x) ,static_cast<float>(y + off) };
		    		else 
		    		return cgm::vec2{ static_cast<float>(res_x) ,static_cast<float>(y - off) };
		    }
		    else {
		    	int res_y = y > (sur.h() / 2) ? sur.h() : -1;//(rand() % 2) == 0 ? -1 : sur.h();
		    	int off = rand() % out_M;

		    	if (x + off < sur.w())
		    		return cgm::vec2{ static_cast<float>(x + off), static_cast<float>(res_y)  };
		    	else
		    		return cgm::vec2{ static_cast<float>(x - off), static_cast<float>(res_y)  };
	        }
	    }

	    bool step_unit(std::unique_ptr<point>& p) 
        {
		    if (p->pos.x() != p->tar.x() || p->pos.y() != p->tar.y())
		    {
		    	if (std::abs(p->pos.x() - p->tar.x()) < 1.0 && std::abs(p->pos.y() - p->tar.y()) < 1.0)
		    	{
		    		p->pos = p->tar;
					p->distance = 0.0f;
		    	}
		    	else
		    	{
		    		move_to(*p);
		    	}
				return false;
		    }
		    else {
		    	p->v.x() = 0.0f;
		    	p->v.y() = 0.0f;
				return true;
		    }
	    }

		virtual void move_to(point& p)
		{
			if(move_to_func)
				move_to_func(p);
			else
				p.pos = p.pos + p.v;
		}

	std::tuple<bool,bool> step() {
		bool use_all_stable = true,out_all_stable = true;
		for (auto& p : use) {
			bool v = step_unit(p);
			if(use_all_stable) use_all_stable = v;
		}
		for (auto& p : out) {
			bool v = step_unit(p);
			if(out_all_stable) out_all_stable = v;
		}
		return std::make_tuple(use_all_stable,out_all_stable);
	};

	void fill() {
		for (auto& p : use) {
			if(custom_pixel)
				sur.set_pixel(static_cast<int>(std::roundf(p->pos.x())), static_cast<int>(std::roundf(p->pos.y())),custom_pixel(
					static_cast<int>(std::roundf(p->pos.x())), static_cast<int>(std::roundf(p->pos.y()))
				));
			else
				sur.set_pixel(static_cast<int>(std::roundf(p->pos.x())), static_cast<int>(std::roundf(p->pos.y())),fill_byte);
		}
		for (auto& p : out) {
			if(custom_pixel)
				sur.set_pixel(static_cast<int>(std::roundf(p->pos.x())), static_cast<int>(std::roundf(p->pos.y())),custom_pixel(
					static_cast<int>(std::roundf(p->pos.x())), static_cast<int>(std::roundf(p->pos.y()))
				));
			else
				sur.set_pixel(static_cast<int>(std::roundf(p->pos.x())), static_cast<int>(std::roundf(p->pos.y())),fill_byte);
		}
	};

    bool good_out_MaxW(int v)
    {
        return v >= 0;
    }  
    int get_out_MaxW()
    {
        return out_MaxW;
    }    
    void set_out_MaxW(int v)
    {
        if(good_out_MaxW(v))
            out_MaxW = v;   
    }

    bool good_out_M(int v)
    {
        return v >= 0;
    }  
    int get_out_M()
    {
        return out_M;
    }    
    void set_out_M(int v)
    {
        if(good_out_M(v))
            out_M = v;   
    }

    bool good_min_frame_ms(int v)
    {
        return v > 0;
    }  
    int get_min_frame_ms()
    {
        return min_frame_ms;
    }    
    void set_min_frame_ms(int v)
    {
        if(good_min_frame_ms(v))
            min_frame_ms = v;   
    }

	void ani_step()
	{
		if constexpr(std::is_same_v<Cnt,wws::cmd_content>)
		{
	    	wws::go_to_xy(0, 0);
		}
	    sur.clear();
	    if (alread_set)
	    {
	    	alread_set = false;
			int ob = static_cast<int>(to_out_speed_min);
			int oe = static_cast<int>(to_out_speed_max) - ob;

			int ub = static_cast<int>(to_use_speed_min);
			int ue = static_cast<int>(to_use_speed_max) - ub;

	    	for (int y = 0; y < last.h(); ++y)
	    	{
	    		for (int x = 0; x < last.w(); ++x)
	    		{
	    			if (back.get_pixel(x, y) != Cnt::EMPTY_PIXEL && last.get_pixel(x, y) == Cnt::EMPTY_PIXEL)
	    			{
	    				auto& p = get_out_to_use();
	    				if (!sur.good_pos(static_cast<int>(p->pos.x()), static_cast<int>(p->pos.y())))
	    					p->pos = rd_out_pos(x, y);
	    				p->tar = cgm::vec2{ static_cast<float>(x),static_cast<float>(y) };
	    				p->v = (p->tar - p->pos).unitized() * (static_cast<float>((rand() % ue) + ub) * to_use_speed);
						p->distance = (p->tar - p->pos).len();
	    			}
	    			else
	    			if (back.get_pixel(x, y) == Cnt::EMPTY_PIXEL && last.get_pixel(x, y) != Cnt::EMPTY_PIXEL)
	    			{
	    				auto& p = get_use_to_out(x, y);
	    				p->pos = cgm::vec2{ static_cast<float>(x),static_cast<float>(y) };
	    				p->tar = rd_out_pos(x, y);
	    				p->v = (p->tar - p->pos).unitized() * (static_cast<float>((rand() % oe) + ob) * to_out_speed);
						p->distance = (p->tar - p->pos).len();
	    			}
	    		}
	    	}
	    }

	    fill();
	    sur.present(drive->get_present());
	    auto [to_use_stable,to_out_stable] = step();
	    auto end2 = std::chrono::system_clock::now();
	    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end2 - start).count();
	    start = std::chrono::system_clock::now();
	    if (duration < min_frame_ms)
	    {
	    	std::this_thread::sleep_for(std::chrono::milliseconds(min_frame_ms - duration));
	    }

	    auto end = std::chrono::system_clock::now();
	    if (drive->need_transfar(
			static_cast<uint32_t>( std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count()),
			to_use_stable,
			to_out_stable))
	    {
	    	drive->step();
	    	back.swap(last);
	    	back.clear();
			if(!drive->is_end())
	    		drive->set_text(back,fill_byte);
	    	alread_set = true;
	    	now = std::chrono::system_clock::now();
	    }
	}

	void pre_go()
	{
		drive->set_text(back,fill_byte);

	    now = std::chrono::system_clock::now();
	    start = std::chrono::system_clock::now();
    
	    alread_set = true;
	}

	bool is_end() const
	{
		return drive->is_end();
	}

    void go()
    {
        pre_go();

	    while (!is_end())
	    {
			ani_step();
	    }
    }

    typename Cnt::PIXEL_TYPE fill_byte;
	std::function<void(point&)> move_to_func;
	std::function<typename Cnt::PIXEL_TYPE(int,int)> custom_pixel;

	float to_use_speed_min = 5.0f;
	float to_use_speed_max = 15.0f;
	float to_use_speed = 0.05f;

	float to_out_speed_min = 5.0f;
	float to_out_speed_max = 15.0f;
	float to_out_speed = 0.05f;

    protected:
        int out_MaxW = 16;
        int out_M = 10;

        std::vector<std::unique_ptr<point>> use;
	    std::vector<std::unique_ptr<point>> out;

        surface<Cnt> sur;
	    surface<Cnt> last;
	    surface<Cnt> back;

        std::chrono::system_clock::time_point now;
	    std::chrono::system_clock::time_point start;

        int min_frame_ms = 18;
	
	    bool alread_set;

        ASDrive<Cnt>* drive = nullptr;
    };

}