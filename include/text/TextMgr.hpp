#pragma once
#include "Page.hpp"
#include "TexGenerate.hpp"
#include "TextRender.hpp"
#include <data_mgr.hpp>
#include "TextNodeGen.hpp"

namespace txt {

	template<typename NodeGen,typename TextGen,typename TextR,template< class A,class B> class PAge>
	struct TextMgr{

		using PageTy = typename PAge<TextGen, TextR>;

		bool has(std::string& font, int flag, int idx, int size, uint32_t c)
		{
			auto key = gen_key(font, flag, idx);
			return has(key, size, c);
		}

		bool has(std::string& key, int size, uint32_t c)
		{
			if (auto it = pages.find(key); it != pages.end())
			{
				if (auto size_it = it->second.find(size); size_it != it->second.end())
				{
					for (const auto& page : size_it->second)
					{
						if (page.has(c))
							return true;
					}
					return false;
				}
				else
					return false;
			}
			else
				return false;
		}

		std::optional<std::reference_wrapper<PageTy>> get_page(std::string& font, int flag, int idx, int size, uint32_t c)
		{
			auto key = gen_key(font, flag, idx);
			if (auto it = pages.find(key); it != pages.end())
			{
				if (auto size_it = it->second.find(size); size_it != it->second.end())
				{
					for (auto& page : size_it->second)
					{
						if (page.has(c))
							return std::make_optional(std::ref(page));
					}
					return {};
				}
				else
					return {};
			}
			else
				return {};
		}

		std::optional<std::reference_wrapper<std::vector<PageTy>>> get_pages_for_size(std::string& font, int flag, int idx, int size)
		{
			auto key = gen_key(font, flag, idx);
			return get_pages_for_size(key, size);
		}

		std::reference_wrapper<std::vector<PageTy>> get_pages_for_size(std::string& key, int size)
		{
			if (auto it = pages.find(key); it != pages.end())
			{
				if (auto size_it = it->second.find(size); size_it != it->second.end())
				{
					return std::ref(size_it->second);
				}
				else
				{
					it->second[size] = std::vector<PageTy>();
					return std::ref(it->second[size]);
				}
			}
			else
			{
				pages[key] = std::unordered_map<int, std::vector<PageTy>>();
				pages[key][size] = std::vector<PageTy>();
				return std::ref(pages[key][size]);
			}
		}

		std::string gen_key(std::string& font, int flag, int idx)
		{
			std::string res(font);
			res += '#';
			res += wws::to_string(flag);
			res += '#';
			res += wws::to_string(idx);
			return res;
		}

		bool put(std::string& font, int flag, int idx, int size, uint32_t c)
		{
			auto key = gen_key(font, flag, idx);
			if (has(key, size, c))
				return true;
			auto tp_pages = get_pages_for_size(key, size);
			
			if(tp_pages.get().empty())
				return put_new_page(font, flag, idx, size, c, tp_pages.get());
			for (const auto& page : tp_pages.get())
			{
				if (page.has(c))
					return true;
			}
			auto& page = tp_pages.get().back();
			if ( page.test(c) )
			{
				return page.put(c);
			}else{
				return put_new_page(font, flag, idx, size, c, tp_pages.get());
			}
		}

		std::tuple<bool,std::optional<std::reference_wrapper<PageTy>>> put_ex(std::string& font, int flag, int idx, int size, uint32_t c)
		{
			auto key = gen_key(font, flag, idx);
			
			auto tp_pages = get_pages_for_size(key, size);
			
			if (tp_pages.get().empty())
			{
				bool f = put_new_page(font, flag, idx, size, c, tp_pages.get());
				return std::make_tuple(f, f ? std::make_optional(std::ref(tp_pages.get().back())) : std::nullopt );
			}
				
			for (auto& page : tp_pages.get())
			{
				if (page.has(c))
					return std::make_tuple(true,std::make_optional(std::ref(page)));
			}
			auto& page = tp_pages.get().back();
			if (page.test(c))
			{
				bool f = page.put(c);
				return std::make_tuple(f, f ? std::make_optional(std::ref(page)) : std::nullopt);
			}
			else {
				bool f = put_new_page(font, flag, idx, size, c, tp_pages.get());
				return std::make_tuple(f, f ? std::make_optional(std::ref(tp_pages.get().back())) : std::nullopt);
			}
		}

		std::tuple<std::shared_ptr<gld::Texture<gld::TexType::D2>>,std::optional<WordData>> get_texture(std::string& font, int flag, int idx, int size, uint32_t c)
		{
			auto[f ,page] = put_ex(font, flag, idx, size, c);
			if (f)
			{
				auto tex = page->get().get_generate();
				auto wd = page->get().get(c);
				return std::make_tuple(tex,wd);
			}
			else {
				return std::make_tuple(nullptr, std::nullopt);
			}
		}
		
		inline static std::shared_ptr<TextMgr> instance()
		{
			if (!self) self = std::shared_ptr<TextMgr>(new TextMgr());
			return self;
		}

		
		std::tuple<std::shared_ptr<gld::Node<gld::Component>>,std::optional<WordData>,int>
			get_node(std::string& font, int flag, int idx, int size, uint32_t c)
		{
			auto[tex,wd] = get_texture(font, flag, idx, size, c);
			if (tex)
			{
				return std::make_tuple( NodeGen::generate(tex, wd.value()) , wd, PageTy::MAXSurfaceSize );
			}
			return std::make_tuple(nullptr, {}, PageTy::MAXSurfaceSize);
		}

	protected:
		bool put_new_page(std::string& font, int flag, int idx, int size, uint32_t c,std::vector<PageTy>& pages)
		{
			auto face = gld::DefDataMgr::instance()->load<gld::DataType::FontFace>(font, flag, idx);
			if (face)
			{
				face->set_pixel_size(size, size);
				pages.push_back(PageTy(face));
				return pages.back().put(c);
			}
			else return false;
		}



		std::unordered_map<std::string,std::unordered_map<int,std::vector<PageTy>>> pages;
		inline static std::shared_ptr<TextMgr> self;
	};


	typedef TextMgr<DefTextNodeGen, TexGenerate, DefTextRender, Page> DefTexMgr;
}