#pragma once 
#include <iostream>
#include <memory>
#include <tuple>
#include <optional>
#include <png.h>
#include <serialization.hpp>
#include <functional>
#include <comm.hpp>

#define PNG_FAILED_CODE 0
namespace png_sundry {
	std::optional<std::tuple<std::unique_ptr<png_byte[]>, std::size_t>> load_png(const char* file, uint32_t format, png_image* image)
	{
		if (!file || !image) return std::nullopt;
		if (png_image_begin_read_from_file(image, file) == PNG_FAILED_CODE)//error
		{
			return std::nullopt;
		}

		std::size_t byte_size;
		image->format = format;
		byte_size = static_cast<std::size_t>(PNG_IMAGE_SIZE(*image));
		std::unique_ptr<png_byte[]> buffer{ new png_byte[byte_size] };

		if (!buffer)
		{
			png_image_free(image);
			return std::nullopt;
		}

		if (png_image_finish_read(image, nullptr, buffer.get(), 0, nullptr) == PNG_FAILED_CODE)
		{
			return std::nullopt;
		}

		return std::make_optional(std::make_tuple(std::move(buffer), byte_size));
	}

	void init_image(png_imagep pimg, uint32_t ver = PNG_IMAGE_VERSION)
	{
		memset(pimg, 0, sizeof(std::remove_pointer_t<png_imagep>));
		pimg->version = ver;
	}

	class hex {
		unsigned int v;
	public:
		hex() : v(0) {}
		hex(unsigned int x) : v(x) {}
		int operator=(unsigned int x) {
			v = x;
			return v;
		}
		operator unsigned int()
		{
			return v;
		}
	};

	enum class OP : unsigned char
	{
		Eq = 0x1,
		NotEq = 0x2,
		Less = 0x4,
		Large = 0x8,
		LessEq = 0x5,
		LargeEq = 0x9
	};

#define has_operator(name,token)																	\
	template<typename T,typename T2>																\
	using has_operator_##name##_t = decltype(std::declval<T>() token std::declval<T2>());			\
																									\
	template <typename T, typename T2>																\
	using has_operator_##name##_vt = wws::is_detected<has_operator_##name##_t, T, T2>;

	has_operator(eq,==)
	has_operator(not_eq,!=)
	has_operator(less,<)
	has_operator(large,>)
	has_operator(less_eq,<=)
	has_operator(large_eq,>=)

#undef has_operator

	class dyn_op {
		OP op;
	public:
		dyn_op() : op(OP::Eq) {}
		dyn_op(OP op) : op(op) {}
		dyn_op(char* str);
		template<typename T,typename T2,typename  = std::enable_if_t< has_operator_eq_vt<T,T2>::value ||
			has_operator_not_eq_vt<T, T2>::value ||
			has_operator_less_vt<T, T2>::value ||
			has_operator_large_vt<T, T2>::value ||
			has_operator_less_eq_vt<T, T2>::value ||
			has_operator_large_eq_vt<T, T2>::value >>
		bool cmp(T&& t,T2&& t2) {
			switch (op)
			{
			case png_sundry::OP::Eq:
				return std::forward<T>(t) == std::forward<T2>(t2);
			case png_sundry::OP::NotEq:
				return std::forward<T>(t) != std::forward<T2>(t2);
			case png_sundry::OP::Less:
				return std::forward<T>(t) < std::forward<T2>(t2);
			case png_sundry::OP::Large:
				return std::forward<T>(t) > std::forward<T2>(t2);
			case png_sundry::OP::LessEq:
				return std::forward<T>(t) <= std::forward<T2>(t2);
			case png_sundry::OP::LargeEq:
				return std::forward<T>(t) >= std::forward<T2>(t2);
			}
		};
	};

	template<typename T, typename = std::enable_if_t< std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_same_v<T,hex>>>
	T parser(const std::string& str, size_t _Base = 10)
	{
		if constexpr (std::is_same_v<T, hex>)
		{
			return wws::parser<unsigned int>(str, 16);
		}
		else {
			return wws::parser<T>(str);
		}
	}

	template<typename ...Ty>
	class FuncArgs {
		std::tuple<Ty...> tup;
	public:
		constexpr static size_t ArgsCount = sizeof...(Ty);
		FuncArgs() : tup()
		{}
		void fill_args(char** p,int begin_i = 0)
		{
			if (p)
				fill_args_impl<0>(p,begin_i);
		}
		template<typename Ret,typename ...Oth>
		Ret run(std::function<Ret(std::tuple<Ty...>&, Oth...)> f,Oth&& ...oth)
		{
			return f(tup, std::forward<Oth>(oth) ...);
		}
	private:
		template<std::size_t I>
		void fill_args_impl(char** p,int i)
		{
			if constexpr (I < std::tuple_size_v<std::tuple<Ty...>>)
			{
				if constexpr (std::is_same_v<char*, std::remove_reference_t<decltype(std::get<I>(tup))>> ||
					std::is_same_v<dyn_op, std::remove_reference_t<decltype(std::get<I>(tup))>>)
				{
					std::get<I>(tup) = p[i];
				}
				else {
					std::get<I>(tup) = parser< std::remove_reference_t<decltype(std::get<I>(tup))>>(p[i]);
				}
				if constexpr (I < (std::tuple_size_v<std::tuple<Ty...>> -1))
				{
					fill_args_impl<I + 1>(p,i + 1);
				}
			}
		}
	};

	template<typename T>
	struct tuple_to_FuncArgs;

	template<typename ...Args>
	struct tuple_to_FuncArgs<std::tuple<Args...>>
	{
		using type = FuncArgs<Args...>;
	};

	template<typename T>
	struct is_tuple {
		static constexpr bool val = false;
	};

	template<typename ...Args>
	struct is_tuple<std::tuple<Args ...>>
	{
		static constexpr bool val = true;
	};

	template <typename Ret,typename Tup,typename ...Oth>
	class FuncWithArgs
	{
		static_assert(is_tuple<Tup>::val,"Must be std::tuple!");
	public:
		using Fir_ty = Tup;	
		using Fir_Args_Ty = typename tuple_to_FuncArgs<Tup>::type;
		using Func_Ty = std::function<Ret(Tup&, Oth...)>;

		Fir_Args_Ty args;
		Func_Ty f;

		FuncWithArgs()
		{}
		
		FuncWithArgs(Fir_Args_Ty args_, Func_Ty f_):args(args_),f(f_)
		{}

		FuncWithArgs( Func_Ty f_) : f(f_)
		{}
		
		Ret operator()(Oth ...oth)
		{
			return args.template run<Ret,Oth ...>(f, std::forward<Oth>(oth) ...);
		}
	};

	struct ArgsNotEqual : public std::exception
	{
		ArgsNotEqual() : std::exception("Args Not Equal!!!") { }
	};

	template<typename Tup, std::size_t I, typename ...Oth>
	void fwa_tup_run_sub(Tup& tup, int i, char** data, int begin_idx, int args_count, Oth&& ...oth)
	{
		static_assert(is_tuple<Tup>::val, "Must be std::tuple!");
		
		if (i == I)
		{
			if (std::get<I>(tup).args.ArgsCount == args_count)
			{
				std::get<I>(tup).args.fill_args(data, begin_idx);
				std::get<I>(tup).operator()(std::forward<Oth>(oth)...);
			}
			else
				throw ArgsNotEqual();
			return;
		}
		if constexpr(I + 1 < std::tuple_size_v<Tup>)
		{
			fwa_tup_run_sub<Tup, I + 1, Oth...>(tup, i, data, begin_idx,args_count, std::forward<Oth>(oth) ...);
		}
	}

	template<typename Tup, typename ...Oth>
	void fwa_tup_run(Tup& tup, int i, char** data, int begin_idx,int args_count,Oth&& ...oth)
	{
		static_assert(is_tuple<Tup>::val, "Must be std::tuple!");
		if constexpr((std::tuple_size_v<Tup>) > 0)
		{
			fwa_tup_run_sub<Tup, 0, Oth...>(tup, i, data, begin_idx,args_count, std::forward<Oth>(oth) ...);
		}
	}
}

#include "sundry_impl.cpp"