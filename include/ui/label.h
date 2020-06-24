#include <event/EventHandler.h>
#include <node.hpp>
#include <component.h>
#include <text/TextMgr.hpp>
#include <node/node_with_event.h>
#include "clip.h"
#include "tools/convert.h"
#include "word.h"

namespace gld {

	enum class Align : int
	{
		Center = 0x0,
		Left,
		Right
	};

	

	struct Label : public Clip {
		Label( std::function<void(float, float)> onSizeChange) : 
			Clip(1.f,1.f,1.f,0.f),
			auto_size(true),
			onSizeChange(onSizeChange)
		{
			
		}

		Label(float w,float h) :
			Clip(w,h,1.f,0.f),auto_size(false)
		{
			
		}

		Label() :
			Clip(1.f, 1.f, 1.f, 0.f),
			auto_size(true)
		{
			
		}

		void set_text(const std::string& txt)
		{
			text = txt;
			if(!txt.empty())
				refresh_ui();
		}

		void refresh_ui()
		{
			if (!node)
				Clip::create();
			else {
				node->remove_all();
			}

			rs.clear();

			auto unicode = cvt::ansi2unicode(text);
			set_word_right(unicode);
		}

		void set_align(Align al)
		{

			if (align != al)
			{
				align = al;
				if (!text.empty())
				{
					if (node && node->children_count() > 0)
					{
						refresh_align();
					}
					else {
						refresh_ui();
					}
				}
			}
		}

		void refresh_align()
		{
			apply_row_offset(width);
		}

		void set_word(const std::wstring& unicode)
		{
			float x = 0.f, y = 0.f, h = static_cast<float>(size) * Word::WORD_SCALE;
			float mw = 0.f, mh = 0.f;
			bool enter = false;

			auto do_enter = [&]() {
				enter = true;
				y -= h + leading * Word::WORD_SCALE;
				if (x > mw) mw = x;
				x = 0.f;
				mh = -y;
			};

			for (auto i = 0; i < unicode.size(); ++i)
			{
				set_word_inside(enter, unicode[i], x, y, do_enter);
			}
			if (x > mw) mw = x;
			if (!enter) mh += h;
			on_text_size_change(mw, mh);

			
			if (auto_size)
			{
				width = mw; height = mh;
			}
			refresh_margin();
		}

		void set_word_right(const std::wstring& unicode)
		{
			float x = 0.f, y = 0.f, h = static_cast<float>(size) * Word::WORD_SCALE;
			float mw = 0.f, mh = 0.f;
			bool enter = false;

			auto do_enter = [&]() {
				Row r;
				enter = true;
				y -= h + leading * Word::WORD_SCALE;
				if (x > mw) mw = x;
				r.w = x;
				x = 0.f;
				mh = -y;
				r.b = rs.empty() ? 0 : rs.back().e;
				r.e = node->children_count();
				r.off_x = node->get_child(r.b)->get_comp<Transform>()->pos.x;
				rs.push_back(r);
			};

			for (auto i = 0; i < unicode.size(); ++i)
			{
				set_word_inside(enter, unicode[i], x, y, do_enter);
			}

			if (!enter && !rs.empty())
			{
				Row r;
				r.w = x;
				r.b = rs.back().e;
				r.e = node->children_count();
				r.off_x = node->get_child(r.b)->get_comp<Transform>()->pos.x;
				rs.push_back(r);
			}

			if (x > mw) mw = x;
			if (!enter) mh += h;

			float max_row_w = mw;
			if (!auto_size) max_row_w = width;

			if(align != Align::Left)
				apply_row_offset(max_row_w);
			
			on_text_size_change(mw, mh);

			if (auto_size)
			{
				width = mw; height = mh;
			}
			refresh_margin();
		}

		void apply_row_offset(float max_row_w)
		{
			
			for (auto i = 0; i < rs.size(); ++i)
			{
				//if (rs[i].w < max_row_w)
				{
					float off_ = -node->get_child(rs[i].b)->get_comp<Transform>()->pos.x + rs[i].off_x;
					float off = off_ + (align == Align::Right ? max_row_w - rs[i].w : (max_row_w - rs[i].w) / 2.f);
					if (align == Align::Left) off = off_;
					for (auto j = rs[i].b; j < rs[i].e; ++j)
					{
						node->get_child(j)->get_comp<Transform>()->pos.x += off;
					}
				}
			}
		}


		void refresh_margin()
		{
			node->get_comp<Transform>()->pos = glm::vec3(margin.x,margin.y,0.f); 
			on_size_change(margin.x + width + margin.z, margin.y + height + margin.w);
		}

		void set_text(std::string&& txt)
		{
			set_text(txt);
		}

		void set_leading(float l)
		{
			leading = l * Word::WORD_SCALE;
		}

		void set_margin(glm::vec4 m)
		{
			margin = m * Word::WORD_SCALE;
		}

		void set_left(float v)
		{
			margin.x = v * Word::WORD_SCALE;
		}

		void set_top(float v)
		{
			margin.y = v * Word::WORD_SCALE;
		}

		void set_right(float v)
		{
			margin.z = v * Word::WORD_SCALE;
		}

		void set_bottom(float v)
		{
			margin.w = v * Word::WORD_SCALE;
		}
		
		std::string text,font = "fonts/SIMHEI.TTF";
		float text_width,text_height;
		bool auto_size = false,
			mulitline = false,
			word_warp = false;
		Align align = Align::Left;
		glm::vec4 color = glm::vec4(1.f);
		std::function<void(float, float)> onSizeChange;
		std::function<void(float, float)> onTextSizeChange;
		glm::vec4 margin = glm::vec4( 0.f,0.f,0.f,0.f );
		int size = 96;
		float leading = 0.f;

		constexpr static float TAB_SIZE = 4;
		constexpr static float SPACE_RATIO = 0.5f;

	protected:
		void on_text_size_change(float w,float h)
		{
			text_width = w; text_height = h;
			if (onTextSizeChange)
				onTextSizeChange(w, h);
		}

		void on_size_change(float w, float h)
		{
			width = w; height = h;
			refresh();
			if (onSizeChange)
				onSizeChange(w, h);
		}

		void set_word_inside(bool & enter,wchar_t c,float& x,float &y,std::function<void()> do_enter)
		{
			enter = false;
			if (c == L' ')
				x += SPACE_RATIO * static_cast<float>(size) * Word::WORD_SCALE;
			else if (c == L'\t')
				x += SPACE_RATIO * static_cast<float>(size) * TAB_SIZE * Word::WORD_SCALE;
			else if (c == L'\n')
				do_enter();
			else {
				auto a = std::shared_ptr<Word>(new Word(font, size, c, glm::vec2(1.f, 0.f)));
				a->load();
				a->get_comp<Transform>()->pos = glm::vec3(x, y - static_cast<float>(a->wd.off_y) * Word::WORD_SCALE, 0.f);
				a->get_comp<txt::DefTextMaterial>()->color = color;

				if (word_warp && x + a->wd.advance * Word::WORD_SCALE > width)
					do_enter();
				else
				{
					x += a->wd.advance * Word::WORD_SCALE;
				}

				node->add_child(a);
			}
		}

		struct Row {
			size_t b = 0, e = 0;
			float w = 0.f, off_x = 0.f;
		};

		std::vector<Label::Row> rs;
	};
}