#pragma once

#include <tuple>
#include <type_traits>
#include <functional>
#include <unordered_map>

#include "RenderComponents.hpp"
#include "RenderStateCache.hpp"
#include "../EcsWorld.hpp"

namespace gld::ecs {

    struct RenderPassContext {
        EcsWorld& world;
        entt::entity camera_entity = entt::null;
        const Camera& camera;
        RenderStateCache& state_cache;
        int target_width = 0;
        int target_height = 0;
    };

    struct RegisteredRenderPassHandler {
        // Reuse the established default render-state policy of a built-in pass
        // (AoE2 units intentionally use BatchPass defaults).
        std::uint32_t default_state_pass_id = RenderPassBatch;
        std::function<void(RenderPassContext&, const RegisteredRenderPass&,
                           const ResolvedRenderPassState&)> render;
        std::function<void(EcsWorld&)> cleanup;
    };

    struct RegisteredRenderPassRegistry {
        std::unordered_map<RegisteredRenderPassId, RegisteredRenderPassHandler> handlers;
    };

    bool register_render_pass(EcsWorld& world, RegisteredRenderPassId id,
                              RegisteredRenderPassHandler handler);
    void unregister_render_pass(EcsWorld& world, RegisteredRenderPassId id);

    template<class Pass>
    struct RenderPassExecutor {
        static void setup(EcsWorld&) {}
        static void cleanup(EcsWorld&) {}
    };

    void render_mesh_pass(RenderPassContext& ctx, const MeshPass& pass);
    void render_batch_pass(RenderPassContext& ctx, const BatchPass& pass);
    void render_fullscreen_pass(RenderPassContext& ctx, const FullscreenRenderPass& pass);
    void cleanup_fullscreen_pass(EcsWorld& w);

    template<>
    struct RenderPassExecutor<MeshPass> {
        static void setup(EcsWorld&) {}
        static void cleanup(EcsWorld&) {}
        static void render(RenderPassContext& ctx, const MeshPass& pass) {
            render_mesh_pass(ctx, pass);
        }
    };

    template<>
    struct RenderPassExecutor<BatchPass> {
        static void setup(EcsWorld&) {}
        static void cleanup(EcsWorld&) {}
        static void render(RenderPassContext& ctx, const BatchPass& pass) {
            render_batch_pass(ctx, pass);
        }
    };

    template<>
    struct RenderPassExecutor<FullscreenRenderPass> {
        static void setup(EcsWorld&) {}
        static void cleanup(EcsWorld& w) {
            cleanup_fullscreen_pass(w);
        }
        static void render(RenderPassContext& ctx, const FullscreenRenderPass& pass) {
            render_fullscreen_pass(ctx, pass);
        }
    };

    template<class T, class Tuple>
    struct TupleContains;

    template<class T>
    struct TupleContains<T, std::tuple<>> : std::false_type {};

    template<class T, class U, class... Rest>
    struct TupleContains<T, std::tuple<U, Rest...>>
        : std::conditional_t<std::is_same_v<T, U>, std::true_type, TupleContains<T, std::tuple<Rest...>>> {};

    template<class Tuple, class Pass>
    struct TupleAppendUnique;

    template<class... Existing, class Pass>
    struct TupleAppendUnique<std::tuple<Existing...>, Pass> {
        using type = std::conditional_t<
            TupleContains<Pass, std::tuple<Existing...>>::value,
            std::tuple<Existing...>,
            std::tuple<Existing..., Pass>>;
    };

    template<class Tuple, class... Passes>
    struct AppendPasses;

    template<class Tuple>
    struct AppendPasses<Tuple> { using type = Tuple; };

    template<class Tuple, class Pass, class... Rest>
    struct AppendPasses<Tuple, Pass, Rest...> {
        using next = typename TupleAppendUnique<Tuple, Pass>::type;
        using type = typename AppendPasses<next, Rest...>::type;
    };

    template<class Tuple, class Component>
    struct AppendComponentPasses;

    template<class Tuple, IRenderPass... Passes>
    struct AppendComponentPasses<Tuple, RenderPasses<Passes...>> {
        using type = typename AppendPasses<Tuple, Passes...>::type;
    };

    template<class Tuple, class... Components>
    struct CollectRegistryPasses;

    template<class Tuple>
    struct CollectRegistryPasses<Tuple> { using type = Tuple; };

    template<class Tuple, class Component, class... Rest>
    struct CollectRegistryPasses<Tuple, Component, Rest...> {
        using next = typename AppendComponentPasses<Tuple, Component>::type;
        using type = typename CollectRegistryPasses<next, Rest...>::type;
    };

    template<class Registry>
    struct RegistryPassTypes;

    template<IRenderPassComponent... Components>
    struct RegistryPassTypes<RenderPassComponentRegistry<Components...>> {
        using type = typename CollectRegistryPasses<std::tuple<>, Components...>::type;
    };

    template<class Tuple>
    struct CleanupPassTuple;

    template<class... Passes>
    struct CleanupPassTuple<std::tuple<Passes...>> {
        static void cleanup(EcsWorld& w) {
            (RenderPassExecutor<Passes>::cleanup(w), ...);
        }
    };

    template<class Registry>
    void cleanup_registered_render_passes(EcsWorld& w) {
        using PassTuple = typename RegistryPassTypes<Registry>::type;
        CleanupPassTuple<PassTuple>::cleanup(w);
    }
}
