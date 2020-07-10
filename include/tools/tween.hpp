#pragma once

#include "executor.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace gld{

    

	struct Tween {

		using TweenFuncTy = std::function<float(float, float, float, float)>;
        


		static float easeOutExpo(float t, float b, float c, float d)
		{
			return t ==  d ? d : d - glm::pow(2.f,-10.f * t);
		}

		Tween(Executor& exec) : exec(exec)
		{}

		template<typename T,typename V>
        void to(std::weak_ptr<T> ptr, V T::* vp, float duration, float b, float e, TweenFuncTy tf, std::function<V(float)> func,
            std::function<void()> complete = nullptr)
		{
            impl([ptr, vp, e, func]() {
                auto p = ptr.lock();
                if (p)
                {
                    auto rp = p.get();
                    (*rp).*vp = func(e);
                }
            },
                [ptr, vp, b, tf, func](float nt, float offset) {
                auto p = ptr.lock();
                if (p)
                {
                    auto rp = p.get();
                    (*rp).*vp = func(tf(nt, 0.f, 1.0f, 1.f) * offset + b);
                }
            }, duration, b, e, tf, complete);
		}

        template<typename T, typename V>
        void to(std::weak_ptr<T> ptr, V T::* vp,float V::* vvp, float duration, float b, float e, TweenFuncTy tf,
            std::function<void()> complete = nullptr)
        {
            impl([ptr, vp, e, vvp]() {
                auto p = ptr.lock();
                if (p)
                {
                    auto rp = p.get();
                    (*rp).*vp.*vvp = e;
                }
            },[ptr, vp,vvp, b, tf](float nt,float offset) {
                auto p = ptr.lock();
                if (p)
                {
                    auto rp = p.get();
                    (*rp).*vp.*vvp = tf(nt, 0.f, 1.0f, 1.f) * offset + b;
                }
            }, duration, b, e, tf, complete);
        }

        template<typename T, typename V>
        void to(std::reference_wrapper<T> ptr,float V::* vvp, float duration, float b, float e, TweenFuncTy tf,
            std::function<void()> complete = nullptr)
        {
                impl(
                    [ptr, e, vvp]() {
                        ptr.get().*vvp = e;
                    },
                    [ptr, vvp, b, tf](float nt,float offset) {
                        ptr.get().*vvp = tf(nt, 0.f, 1.0f, 1.f) * offset + b;
                    },
                    duration, b, e, tf, complete);
        }

        void to(std::function<void(float)> on,float duration, float b, float e, TweenFuncTy tf,
            std::function<void()> complete = nullptr)
        {
            impl(
                [on, e]() {
                on(e);
            },
                [on,b, tf](float nt, float offset) {
                 on(tf(nt, 0.f, 1.0f, 1.f) * offset + b);
            },
                duration, b, e, tf, complete);
        }

        void impl(std::function<void()> end_f,std::function<void(float,float)> progress_f,float duration, float b, float e, TweenFuncTy tf,
            std::function<void()> complete = nullptr)
        {
            float t = 0.f;
            while (1)
            {
                if (t + (float)ms >= duration)
                {
                    exec.delay(end_f, duration);
                    if (complete) exec.delay(complete, duration);
                    break;
                }
                float nt = t / duration;
                float offset = e - b;
                exec.delay([nt,offset,progress_f]() {
                    progress_f(nt, offset);
                }, t);
                t += (float)ms;
            }
        }

        float get_dir(float b, float e)
        {
            return b < e ? 1.f : -1.f;
        }

        std::function<bool(float,float)> get_end_condition(float b, float e)
        {
            return b < e ? large_eq : less_eq;
        }

        inline static bool large_eq(float a,float b)
        {
            return a >= b;
        }

        inline static bool less_eq(float a, float b)
        {
            return a <= b;
        }

		int ms = 16;
		Executor& exec;
	};

    namespace tween {
        static float linear(float t, float b, float c, float d)
        {
            return c * t / d + b;
        }

        namespace Quad {
            static float easeIn(float t, float b, float c, float d)
            {
                return c * (t /= d) * t + b;
            }
            static float easeOut(float t, float b, float c, float d)
            {
                return -c * (t /= d) * (t - 2.f) + b;
            }
            static float easeInOut(float t, float b, float c, float d)
            {
                if ((t /= d / 2.f) < 1.f) return c / 2.f * t * t + b;
                return -c / 2.f * ((--t) * (t - 2.f) - 1.f) + b;
            }
        }
        namespace Cubic {
            static float easeIn(float t, float b, float c, float d)
            {
                return c * (t /= d) * t * t + b;
            }
            static float easeOut(float t, float b, float c, float d)
            {
                return c * ((t = t / d - 1.f) * t * t + 1.f) + b;
            }
            static float easeInOut(float t, float b, float c, float d)
            {
                if ((t /= d / 2.f) < 1.f) return c / 2.f * t * t * t + b;
                return c / 2.f * ((t -= 2.f) * t * t + 2.f) + b;
            }
        }

    
        namespace Quart {
            static float easeIn(float t, float b, float c, float d)
            {
                return c * (t /= d) * t * t * t + b;
            }
            static float easeOut(float t, float b, float c, float d)
            {
                return -c * ((t = t / d - 1.f) * t * t * t - 1.f) + b;
            }
            static float easeInOut(float t, float b, float c, float d)
            {
                if ((t /= d / 2.f) < 1.f) return c / 2.f * t * t * t * t + b;
                return -c / 2.f * ((t -= 2.f) * t * t * t - 2.f) + b;
            }
        }
        namespace Quint {
            static float easeIn(float t, float b, float c, float d)
            {
                return c * (t /= d) * t * t * t * t + b;
            }
            static float easeOut(float t, float b, float c, float d)
            {
                return c * ((t = t / d - 1.f) * t * t * t * t + 1.f) + b;
            }
            static float easeInOut(float t, float b, float c, float d)
            {
                if ((t /= d / 2.f) < 1.f) return c / 2.f * t * t * t * t * t + b;
                return c / 2.f * ((t -= 2.f) * t * t * t * t + 2.f) + b;
            }
        }
        namespace Sine {
            static float easeIn(float t, float b, float c, float d)
            {
                return -c * glm::cos(t / d * (glm::pi<float>() / 2.f)) + c + b;
            }
            static float easeOut(float t, float b, float c, float d)
            {
                return c * glm::sin(t / d * (glm::pi<float>() / 2.f)) + b;
            }
            static float easeInOut(float t, float b, float c, float d)
            {
                return -c / 2.f * (glm::cos(glm::pi<float>() * t / d) - 1.f) + b;
            }
        }
        namespace Expo {
            static float easeIn(float t, float b, float c, float d)
            {
                return (t == 0.f) ? b : c * glm::pow(2.f, 10.f * (t / d - 1.f)) + b;
            }
            static float easeOut(float t, float b, float c, float d)
            {
                return (t == d) ? b + c : c * (-glm::pow(2.f, -10.f * t / d) + 1.f) + b;
            }
            static float easeInOut(float t, float b, float c, float d)
            {
                if (t == 0.f) return b;
                if (t == d) return b + c;
                if ((t /= d / 2.f) < 1.f) return c / 2.f * glm::pow(2.f, 10.f * (t - 1.f)) + b;
                return c / 2.f * (-glm::pow(2.f, -10.f * --t) + 2.f) + b;
            }
        }
        namespace Circ {
            static float easeIn(float t, float b, float c, float d)
            {
                return -c * (glm::sqrt(1.f - (t /= d) * t) - 1.f) + b;
            }
            static float easeOut(float t, float b, float c, float d)
            {
                return c * glm::sqrt(1.f - (t = t / d - 1.f) * t) + b;
            }
            static float easeInOut(float t, float b, float c, float d)
            {
                if ((t /= d / 2.f) < 1) return -c / 2.f * (glm::sqrt(1.f - t * t) - 1.f) + b;
                return c / 2.f * (glm::sqrt(1.f - (t -= 2.f) * t) + 1.f) + b;
            }
        }
        namespace Bounce {
            
            static float easeOut(float t, float b, float c, float d)
            {
                if ((t /= d) < (1.f / 2.75f)) {
                    return c * (7.5625f * t * t) + b;
                }
                else if (t < (2.f / 2.75f)) {
                    return c * (7.5625f * (t -= (1.5f / 2.75f)) * t + 0.75f) + b;
                }
                else if (t < (2.5f / 2.75f)) {
                    return c * (7.5625f * (t -= (2.25f / 2.75f)) * t + 0.9375f) + b;
                }
                else {
                    return c * (7.5625f * (t -= (2.625f / 2.75f)) * t + 0.984375f) + b;
                }
            }
            static float easeIn(float t, float b, float c, float d)
            {
                return c - easeOut(d - t, 0.f, c, d) + b;
            }
            static float easeInOut(float t, float b, float c, float d)
            {
                if (t < d / 2.f) return easeIn(t * 2.f, 0.f, c, d) * 0.5f + b;
                else return easeOut(t * 2.f - d, 0.f, c, d) * 0.5f + c * 0.5f + b;
            }
        }
        namespace Back {
            static float easeIn(float t, float b, float c, float d)
            {
                float s = 1.70158f;
                return c * (t /= d) * t * ((s + 1.f) * t - s) + b;
            }
            static float easeOut(float t, float b, float c, float d)
            {
                float s = 1.70158f;
                return c * ((t = t / d - 1.f) * t * ((s + 1.f) * t + s) + 1.f) + b;
            }
            static float easeInOut(float t, float b, float c, float d)
            {
                float s = 1.70158f;
                if ((t /= d / 2.f) < 1.f) return c / 2.f * (t * t * (((s *= (1.525f)) + 1.f) * t - s)) + b;
                return c / 2.f * ((t -= 2.f) * t * (((s *= (1.525f)) + 1.f) * t + s) + 2.f) + b;
            }
        }

    }
}