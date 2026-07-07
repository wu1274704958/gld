#pragma once
#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftglyph.h>

#include <iostream>
#include <functional>
#include <optional>
#include <atomic>

namespace ft2 {
	class Library {
	public:
		Library() {
			if (InsteNum >= 1)
			{
				throw std::runtime_error("Has an instance!");
			}
			++InsteNum;
			FT_Init_FreeType(&lib);
		}
		~Library() {
			--InsteNum;
			if(FaceInstanceNum <= 0)
				FT_Done_FreeType(lib);
			else
			{
				BeLeftLib = lib;
				lib = nullptr;
				NeedDoneLib = true;
			}
		}
		Library(const Library&) = delete;
		Library(Library&&) = delete;
		Library& operator=(const Library&) = delete;
		Library& operator=(Library&&) = delete;

		template<typename T>
		T load_face(const char* path, int idx = 0)
		{
			return T(lib,path,idx);
		}

		template<typename T>
		T load_face_for_mem(const unsigned char* data,size_t size,int idx = 0)
		{
			return T(lib,data,size,idx);
		}

		inline static int get_InsteNum()
		{
			return InsteNum;
		}

		inline static void create_face()
		{
			++FaceInstanceNum;
		}

		inline static void done_face()
		{
			--FaceInstanceNum;
			if(FaceInstanceNum == 0 && NeedDoneLib && BeLeftLib)
				FT_Done_FreeType(BeLeftLib);
		}

	private:
		FT_Library lib = nullptr;
		static std::atomic<int> InsteNum;
		static std::atomic<int> FaceInstanceNum;
		static std::atomic<bool> NeedDoneLib;
		static FT_Library BeLeftLib;
	};

	inline std::atomic<int> Library::InsteNum = 0;
	inline std::atomic<int> Library::FaceInstanceNum = 0;
	inline std::atomic<bool> Library::NeedDoneLib = false;
	inline FT_Library Library::BeLeftLib = nullptr;

	class Face {
	public:
		Face() {

		}
		Face(FT_Library lib,const char*path,int idx) {
			int error;
			if ((error = FT_New_Face(lib, path, idx, &face)) == FT_Err_Unknown_File_Format)
			{
				throw std::runtime_error("Unknown File Format");
			}
			else if (error == FT_Err_Cannot_Open_Resource)
			{
				throw std::runtime_error("Cannot Open Resource");
			}
			if (face)
				Library::create_face();
		}
		Face(FT_Library lib,const unsigned char*data,size_t size,int idx) {
			int error;
			if ((error = FT_New_Memory_Face(lib, data,size,idx, &face)) == FT_Err_Unknown_File_Format)
			{
				throw std::runtime_error("Unknown File Format");
			}
			else if (error == FT_Err_Cannot_Open_Resource)
			{
				throw std::runtime_error("Cannot Open Resource");
			}
			if (face)
				Library::create_face();
		}
		Face(const Face&) = delete;
		Face(Face&& oth) {
			face = oth.face;
			oth.face = nullptr;
		};
		Face& operator=(const Face&) = delete;
		Face& operator=(Face&& oth)
		{
			done();
				
			face = oth.face;
			oth.face = nullptr;
			return *this;
		}
		~Face() {
			done();
		}

		void set_pixel_size(uint32_t w,uint32_t h) {
			if(face)
				FT_Set_Pixel_Sizes(face, w, h);
		}

		void select_charmap(FT_Encoding_ cm)
		{
			if (face)
				FT_Select_Charmap(face, cm);
		}
		
		void load_glyph(FT_ULong c, uint32_t load_flag = FT_LOAD_DEFAULT, FT_Render_Mode render_flag = FT_RENDER_MODE_MONO)
		{
			if (face)
			{
				uint32_t n1 = FT_Get_Char_Index(face, c);
				FT_Load_Glyph(face, n1, load_flag);
				FT_Render_Glyph(face->glyph, render_flag);
			}
		}

		FT_GlyphSlot glyph_slot()
		{
			return face->glyph;
		}

		template<typename Sur, typename Ret,typename ...Oth>
		int render_surface(Sur& sur, Ret(Sur::* set_pixel)(int,int,Oth...) ,int bx,int by,Oth ...oth)
		{
			if (!face)
				return 0;
			auto gs = glyph_slot();
			auto bits = &gs->bitmap;
			constexpr int CS = sizeof(char) * 8;

			/*FT_Glyph glyph;
			FT_Get_Glyph(gs, &glphy);

			FT_BBox box;
			FT_Glyph_Get_CBox(glyph, FT_GLYPH_BBOX_PIXELS, &box);*/

			int a = gs->bitmap_left + bx;
			int b = (gs->face->size->metrics.y_ppem) - bits->rows + by;

			/*for (int i = 0; i < bits->rows * bits->pitch; ++i)
			{
				printBin(bits->buffer[i],false);
				if ((i + 1) % bits->pitch == 0)
					std::cout << "\n";
			}*/

			for (unsigned int y = 0; y < bits->rows; ++y)
			{
				for (int x = 0; x < bits->pitch * CS; ++x)
				{
					if ((bits->buffer[(y * bits->pitch) + (x / CS)] << (x % CS)) & 0x80)
					{
						(sur.*set_pixel)(a + x, b + y,std::forward<Oth>(oth)...);
					}
				}
			}

			return gs->advance.x / 64;
		}

		template<typename Sur, typename Ret,typename CustomOff ,typename ...Oth>
		int render_surface(Sur& sur,CustomOff custom_off, Ret(Sur::* set_pixel)(int,int,Oth...) ,int bx,int by,Oth ...oth)
		{
			if (!face)
				return 0;
			auto gs = glyph_slot();
			auto bits = &gs->bitmap;
			constexpr int CS = sizeof(char) * 8;

			int a = custom_off.off_x(gs) + bx;
			int b = custom_off.off_y(gs) + by;

			for (unsigned int y = 0; y < bits->rows; ++y)
			{
				for (int x = 0; x < bits->pitch * CS; ++x)
				{
					if ((bits->buffer[(y * bits->pitch) + (x / CS)] << (x % CS)) & 0x80)
					{
						(sur.*set_pixel)(a + x, b + y,std::forward<Oth>(oth)...);
					}
				}
			}

			return gs->advance.x / 64;
		}

		template<typename Sur, typename Ret,typename CustomOff,typename PT>
		int render_surface(Sur& sur,CustomOff custom_off, Ret(Sur::* set_pixel)(int,int,PT) ,int bx,int by,
			std::function<PT(int,int)> custom_pixel)
		{
			if (!face)
				return 0;
			auto gs = glyph_slot();
			auto bits = &gs->bitmap;
			constexpr int CS = sizeof(char) * 8;

			int a = custom_off.off_x(gs) + bx;
			int b = custom_off.off_y(gs) + by;

			for (unsigned int y = 0; y < bits->rows; ++y)
			{
				for (int x = 0; x < bits->pitch * CS; ++x)
				{
					if ((bits->buffer[(y * bits->pitch) + (x / CS)] << (x % CS)) & 0x80)
					{
						(sur.*set_pixel)(a + x, b + y,custom_pixel(a + x,b + y));
					}
				}
			}

			return gs->advance.x / 64;
		}
		
		template<typename CustomOff>
		std::optional<std::tuple<int,int,int,unsigned int, unsigned int>> get_glyph_data(CustomOff custom_off)
		{
			if (!face)
				return {};
			auto gs = glyph_slot();
			auto bits = &gs->bitmap;
			constexpr int CS = sizeof(char) * 8;

			int a = custom_off.off_x(gs);
			int b = custom_off.off_y(gs);

			return std::make_optional(std::make_tuple(a, b, static_cast<int>(gs->advance.x / 64), bits->width, bits->rows));
		}
	protected:
		void done()
		{
			if (face)
			{
				FT_Done_Face(face);
				Library::done_face();
			}
		}
	private:
		FT_Face face = nullptr;
	};

	struct CenterOff
	{
		int off_x(FT_GlyphSlot& gs)
		{
			return gs->bitmap_left;
		}
		int off_y(FT_GlyphSlot& gs)
		{
			return gs->face->size->metrics.ascender/64 -  gs->face->glyph->bitmap_top;
		}
	};

	struct CenterOffEx
	{
		int off_x(FT_GlyphSlot& gs)
		{
			return gs->bitmap_left;
		}
		int off_y(FT_GlyphSlot& gs)
		{
			int oy = gs->face->size->metrics.ascender/64 - gs->face->glyph->bitmap_top;
			if(oy + gs->face->glyph->bitmap.rows > gs->face->size->metrics.y_ppem)
				oy -= (oy + gs->face->glyph->bitmap.rows - gs->face->size->metrics.y_ppem);
			return oy;
		}
	};

	struct AlwaysZero
	{
		int off_x(FT_GlyphSlot& gs)
		{
			return 0;
		}
		int off_y(FT_GlyphSlot& gs)
		{
			return 0;
		}
	};

}