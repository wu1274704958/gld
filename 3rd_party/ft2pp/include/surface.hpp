#pragma once 

#include <iostream>

namespace wws {

	class cmd_content {
	protected:
		char* ptr = nullptr;
		int w;
		int h;
	public:
		using PIXEL_TYPE = char;
		using PRESENT_ARGS_TYPE = std::ostream&;
		constexpr static PIXEL_TYPE PIXEL_ZERO = 0;
		constexpr static PIXEL_TYPE EMPTY_PIXEL = ' ';
		cmd_content(int w, int h) {
			this->w = w + 1;
			this->h = h;
			ptr = new char[this->w * this->h];
		}
		~cmd_content() {
			if (ptr)
			{
				delete[] ptr;
			}
		}
		cmd_content(const cmd_content&) = delete;
		cmd_content(cmd_content&& oth)
		{
			ptr = oth.ptr;
			oth.ptr = nullptr;

			w = oth.w;
			h = oth.h;
		}
		cmd_content& operator=(const cmd_content&) = delete;
		cmd_content& operator=(cmd_content&& oth)
		{
			if (ptr)
			{
				delete[] ptr;
				ptr = nullptr;
			}
			ptr = oth.ptr;
			oth.ptr = nullptr;
			w = oth.w;
			h = oth.h;
			return *this;
		}

		virtual void init()
		{
			for (int i = 1; i < w * h; ++i)
			{
				if (i % w == 0)
					ptr[i - 1] = '\n';
				else 
					ptr[i - 1] = ' ';
			}
			ptr[w * h - 1] = '\0';
		}

		virtual void set_pixel(int x, int y, PIXEL_TYPE p)
		{
			ptr[(y * w) + x] = p;
		}

		virtual PIXEL_TYPE get_pixel(int x, int y) const
		{
			return ptr[(y * w) + x];
		}

		virtual void swap(cmd_content& oth)
		{
			auto temp = oth.ptr;
			oth.ptr = this->ptr;
			this->ptr = temp;

			auto tt = oth.w;
			oth.w = w;
			w = tt;

			tt = oth.h;
			oth.h = h;
			h = tt;
		}

		virtual void present(PRESENT_ARGS_TYPE a) const
		{
			a << ptr << "\n";
		}

		void clear()
		{
			init();
		}

		

	};

	template<typename Cnt>
	class surface{
	protected:
		int width;
		int height;

		Cnt content;

		using PIXEL_TYPE = typename Cnt::PIXEL_TYPE;
		using PRESENT_ARGS_TYPE = typename Cnt::PRESENT_ARGS_TYPE;

		constexpr static PIXEL_TYPE EMPTY_PIXEL = Cnt::EMPTY_PIXEL;
		constexpr static PIXEL_TYPE PIXEL_ZERO = Cnt::PIXEL_ZERO;

	public:
		surface(int w, int h) : 
			content(w,h),
			width(w),
			height(h)
		{
			content.init();
		}

		surface(const surface<Cnt>&) = delete;
		surface(surface<Cnt>&& oth) = default;

		surface<Cnt>& operator=(const surface<Cnt>&) = delete;
		surface<Cnt>& operator=(surface<Cnt>&& oth) = default;

		bool good_pos(int x, int y) const
		{
			return x >= 0 && x < width && y >= 0 && y < height;
		}

		void set_pixel(int x, int y, PIXEL_TYPE p)
		{
			if(good_pos(x,y))
				content.set_pixel(x,y,p);
		}

		PIXEL_TYPE get_pixel(int x, int y) const
		{
			if (good_pos(x, y))
				return content.get_pixel(x, y);
			return Cnt::PIXEL_ZERO;
		}

		void present(PRESENT_ARGS_TYPE a) const
		{
			content.present(a);
		}

		void clear()
		{
			content.clear();
		}

		void swap(surface<Cnt>& oth)
		{
			content.swap(oth.content);
		}

		int w() const
		{
			return width;
		}

		int h() const
		{
			return height;
		}

		const Cnt& get_content() const
		{
			return content;
		}
		

	};

	typedef surface<cmd_content> CmdSurface;
}