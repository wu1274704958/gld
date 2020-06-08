#pragma once

#include <functional>

namespace txt {

	class GrayContent {
	public:
		unsigned char* ptr = nullptr;
		int w;
		int h;

		using PIXEL_TYPE = unsigned char;
		using PRESENT_ARGS_TYPE = std::function<void(const GrayContent*)>;
		constexpr static PIXEL_TYPE PIXEL_ZERO = 0x0;
		constexpr static PIXEL_TYPE EMPTY_PIXEL = 0x0;
		GrayContent(int w, int h) {
			this->w = w;
			this->h = h;
			ptr = new unsigned char[this->w * this->h];
		}
		~GrayContent() {
			if (ptr)
			{
				delete[] ptr;
			}
		}
		GrayContent(const GrayContent&) = delete;
		GrayContent(GrayContent&& oth)
		{
			ptr = oth.ptr;
			oth.ptr = nullptr;
			w = oth.w;
			h = oth.h;
		}
		GrayContent& operator=(const GrayContent&) = delete;
		GrayContent& operator=(GrayContent&& oth)
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

		virtual void swap(GrayContent& oth)
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
			a(this);
		}

		void clear()
		{
			init();
		}

	};
}