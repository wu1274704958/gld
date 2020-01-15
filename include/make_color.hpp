#pragma once
#include <iostream>
#include "dbg.hpp"
#include "comm.hpp"

namespace wws {

	template<char C1, char C2>
	struct FormHex {
		template<char C>
		constexpr static unsigned char get()
		{
			if constexpr (C >= '0' && C <= '9')
			{
				return C - '0';
			}
			else if constexpr (C >= 'A' && C <= 'F') {
				return 10 + (C - 'A');
			}
			else if constexpr (C >= 'a' && C <= 'f') {
				return 10 + (C - 'a');
			}
			return 0;
		}
		constexpr static unsigned char val = get<C1>() * 16 + get<C2>();
	};

	template <char C1, char C2>
	constexpr unsigned char form_hex()
	{
		return FormHex<C1, C2>::val;
	}

	template<char ...CS>
	struct string {
		template<std::size_t I>
		constexpr static char get()
		{
			if constexpr (I < 0 || I > len_impl<1, CS...>())
				return 0;
			else
				return get_impl<0, I, CS...>();
		}

		template<std::size_t B, std::size_t I, char Curr, char ...Cs>
		constexpr static char get_impl()
		{
			if constexpr (B == I)
			{
				return Curr;
			}
			else {
				return get_impl<B + 1, I, Cs...>();
			}
		}

		template<std::size_t B, std::size_t I>
		constexpr static char get_impl()
		{
			return 0;
		}

		constexpr static size_t len()
		{
			return len_impl<1, CS...>();
		}

		template<std::size_t B, char Curr, char ...Cs>
		constexpr static char len_impl()
		{
			if constexpr (sizeof...(Cs) == 0)
			{
				return B;
			}
			else {
				return len_impl<B + 1, Cs...>();
			}
		}
	};

	template <typename S, std::size_t ...N>
	constexpr string<S::get()[N]...>
		prepare_impl(S, std::index_sequence<N...>)
	{
		return {};
	}

	template <typename S>
	constexpr decltype(auto) prepare(S s) {
		return prepare_impl(s,
			std::make_index_sequence< sizeof(S::get()) - 1>{});
	}

#define has_color_(feild)									\
template <class T>											\
using has_##feild##_t = decltype(std::declval<T>().feild);	\
															\
template <typename T>										\
using has_##feild##_vt = wws::is_detected<has_##feild##_t,T>;	

	has_color_(r)
	has_color_(g)
	has_color_(b)
	has_color_(a)

#undef has_color_

#define has_color_(func_name)									\
template <class T>											\
using has_fn_##func_name##_t = decltype(std::declval<T>().func_name());	\
															\
template <typename T>										\
using has_fn_##func_name##_vt = wws::is_detected<has_fn_##func_name##_t,T>;

    has_color_(r)
    has_color_(g)
    has_color_(b)
    has_color_(a)

#undef has_color_


	template<typename T>
	struct rgba;

	template<char ... Cs>
	struct rgba<string<Cs...>>
	{
		using type = string<Cs...>;
		static constexpr size_t ALLOW_LEN = type::template get<0>() == '#' ? 9 : 8;
		static_assert(type::len() == ALLOW_LEN);
		static constexpr size_t BEGIN_I = type::template get<0>() == '#' ? 1 : 0;
		static constexpr float value[4] = {
			(float)(form_hex<type::template get<BEGIN_I + 0>(),type::template get<BEGIN_I + 1>()>()) / 255.0f  ,
			(float)(form_hex<type::template get<BEGIN_I + 2>(),type::template get<BEGIN_I + 3>()>()) / 255.0f  ,
			(float)(form_hex<type::template get<BEGIN_I + 4>(),type::template get<BEGIN_I + 5>()>()) / 255.0f  ,
			(float)(form_hex<type::template get<BEGIN_I + 6>(),type::template get<BEGIN_I + 7>()>()) / 255.0f
		};

		template<typename T>
		T make()
		{
			static_assert((has_r_vt<T>::value &&
				has_g_vt<T>::value &&
				has_b_vt<T>::value &&
				has_a_vt<T>::value) ||
                (has_fn_r_vt<T>::value &&
                has_fn_g_vt<T>::value &&
                has_fn_b_vt<T>::value &&
                has_fn_a_vt<T>::value), "This type must has member named r,g,b,a!!!");
			T t;
            if constexpr (has_r_vt<T>::value &&
                          has_g_vt<T>::value &&
                          has_b_vt<T>::value &&
                          has_a_vt<T>::value) {
                t.r = value[0];
                t.g = value[1];
                t.b = value[2];
                t.a = value[3];
            }else{
                t.r() = value[0];
                t.g() = value[1];
                t.b() = value[2];
                t.a() = value[3];
            }
			return t;
		}
	};

	template<typename T>
	struct rgb;

	template<char ... Cs>
	struct rgb<string<Cs...>>
	{
		using type = string<Cs...>;
		static constexpr size_t ALLOW_LEN = type::template get<0>() == '#' ? 7 : 6;
		static_assert(type::len() == ALLOW_LEN);
		static constexpr size_t BEGIN_I = type::template get<0>() == '#' ? 1 : 0;
		static constexpr float value[3] = {
			(float)(form_hex<type::template get<BEGIN_I + 0>(),type::template get<BEGIN_I + 1>()>()) / 255.0f  ,
			(float)(form_hex<type::template get<BEGIN_I + 2>(),type::template get<BEGIN_I + 3>()>()) / 255.0f  ,
			(float)(form_hex<type::template get<BEGIN_I + 4>(),type::template get<BEGIN_I + 5>()>()) / 255.0f
		};

		template<typename T>
		T make()
		{
			static_assert(
                    (has_r_vt<T>::value &&
				    has_g_vt<T>::value &&
				    has_b_vt<T>::value) ||
				    (has_fn_r_vt<T>::value &&
                     has_fn_g_vt<T>::value &&
                     has_fn_b_vt<T>::value) , "This type must has member named r,g,b,a!!!");
			T t;
			if constexpr (has_r_vt<T>::value &&
                          has_g_vt<T>::value &&
                          has_b_vt<T>::value) {
                t.r = value[0];
                t.g = value[1];
                t.b = value[2];
            }else{
                t.r() = value[0];
                t.g() = value[1];
                t.b() = value[2];
			}
			return t;
		}
	};



	template<char ...Cs>
	auto make_rgba(string<Cs...> str) -> rgba<string<Cs...>>
	{
		return {};
	}

	template<char ...Cs>
	auto make_rgb(string<Cs...> str) -> rgb<string<Cs...>>
	{
		return {};
	}

}


#define PREPARE_STRING(s)                                                \
    (::wws::prepare([]{														\
        struct tmp {                                                        \
            static constexpr decltype(auto) get() { return s; }             \
        };                                                                  \
        return tmp{};                                                       \
    }())) 