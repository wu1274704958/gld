//
// Created by wushu on 2020/2/22.
//

#ifndef GLD_MAPEVENT_H
#define GLD_MAPEVENT_H

#include <gl_comm.hpp>
#include <unordered_map>
#ifdef PF_ANDROID
#include <GlfwDef.h>
#include <android/input.h>
#endif
namespace gld {

    template<typename ...T>
    struct MapEvent {

        template<size_t K>
        constexpr static size_t map() {
            return map_sub<K, T...>();
        }

        template<size_t K, typename F, typename ...Args>
        constexpr static size_t map_sub() {
            if constexpr (F::v1 == K) {
                return F::template map<K>();
            } else {
                if constexpr(sizeof...(Args) > 0) {
                    return map_sub<K, Args...>();
                } else {
                    return static_cast<size_t>(-1);
                }
            }
        }

        template<size_t N>
        constexpr static size_t val = map<N>();

        inline static bool is_init() { return runtime_map_init;}
        inline static void init_runtime_map()
        {
            ((runtime_map.insert(std::make_pair(T::v1,T::v2))),...);
            runtime_map_init = true;
        }
        inline static size_t map_ex(size_t k)
        {
            if(runtime_map_init)
            {
                if(runtime_map.find(k) != runtime_map.end())
                    return runtime_map[k];
            }
            return static_cast<size_t>(-1);
        }
    protected:
        inline static bool runtime_map_init = false;
        inline static std::unordered_map<size_t ,size_t> runtime_map;

    };

#ifdef PF_ANDROID
    using EventMap = MapEvent<
            Vmv<AMOTION_EVENT_ACTION_DOWN,GLFW_PRESS>,
            Vmv<AMOTION_EVENT_ACTION_UP,GLFW_RELEASE>,
            Vmv<AMOTION_EVENT_ACTION_MOVE,GLFW_REPEAT>
            >;
#endif
}
#endif //GLD_MAPEVENT_H
