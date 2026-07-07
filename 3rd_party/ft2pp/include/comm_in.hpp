#pragma once

namespace wws{
    void go_to_xy(int x,int y);


    struct ColorWrapRgba {

		ColorWrapRgba()
		{
			set(0x0);
		}

		ColorWrapRgba(unsigned int v)
		{
			set(v);
		}

		ColorWrapRgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
		{
			set_r(r);
			set_g(g);
			set_b(b);
			set_a(a);
		}

        unsigned char data[4];

        void set_r(unsigned char v)
        {
            data[0] = v;
        }
        void set_g(unsigned char v)
        {
            data[1] = v;
        }
        void set_b(unsigned char v)
        {
            data[2] = v;
        }
        void set_a(unsigned char v)
        {
            data[3] = v;
        }

        unsigned char r()
        {
            return data[0];
        }
        unsigned char g()
        {
            return data[1];
        }
        unsigned char b()
        {
            return data[2];
        }
        unsigned char a()
        {
            return data[3];
        }

		unsigned int operator=(unsigned int v)
		{
			set(v);
			return v;
		}

        void set(unsigned int v)
        {
            int* p = reinterpret_cast<int*>(&data);
            *p = v;
        }
    };

	class rgba_content {
	public:
		using PIXEL_TYPE = ColorWrapRgba;
	protected:
		PIXEL_TYPE* ptr = nullptr;
		int w;
		int h;
	public:
		
		constexpr static unsigned int PIXEL_ZERO = 0x0;
		constexpr static unsigned int EMPTY_PIXEL = 0xff000000;
		rgba_content(int w, int h) {
			this->w = w;
			this->h = h;
			ptr = new PIXEL_TYPE[this->w * this->h];
		}
		~rgba_content() {
			if (ptr)
			{
				delete[] ptr;
			}
		}
		rgba_content(const rgba_content&) = delete;
		rgba_content(rgba_content&& oth)
		{
			ptr = oth.ptr;
			oth.ptr = nullptr;

			w = oth.w;
			h = oth.h;
		}
		rgba_content& operator=(const rgba_content&) = delete;
		rgba_content& operator=(rgba_content&& oth)
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

		PIXEL_TYPE& operator[](size_t i)
		{
			return ptr[i];
		}

		const PIXEL_TYPE& operator[](size_t i) const
		{
			return ptr[i];
		}

		virtual void init()
		{
			for (int i = 0; i < w * h; ++i)
			{
				ptr[i] = PIXEL_ZERO;
			}
		}

		virtual void set_pixel(int x, int y, PIXEL_TYPE p)
		{
			ptr[(y * w) + x] = p;
		}

		virtual PIXEL_TYPE& get_pixel(int x, int y)
		{
			return ptr[(y * w) + x];
		}

		virtual const PIXEL_TYPE& get_pixel(int x, int y) const
		{
			return ptr[(y * w) + x];
		}

		virtual void swap(rgba_content& oth)
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

		PIXEL_TYPE* get()
		{
			return ptr;
		}

		void clear()
		{
			init();
		}

		int width()
		{
			return w;
		}

		int height()
		{
			return h;
		}
	};

}