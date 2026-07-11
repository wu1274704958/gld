#pragma once

// RenderGraph – default render-pass scheduler for ECS rendering.
//
// Phase 1 mirrors every Camera entity into a graph node and executes camera
// nodes through topological order. With no explicit edges, priority/entity order
// matches the old camera sorter. Later phases can add fullscreen/post-process
// node kinds and resource edges without replacing the scheduler again.

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

namespace gld::ecs {

    using RenderGraphNodeId = std::uint64_t;

    enum class RenderGraphNodeKind : std::uint8_t {
        Camera = 0,
    };

    struct RenderGraphNode {
        RenderGraphNodeId id = 0;
        RenderGraphNodeKind kind = RenderGraphNodeKind::Camera;
        entt::entity entity = entt::null;
        int priority = 0;
        bool enabled = true;
        bool generated = false;
        std::uint64_t owner = 0;
        std::string name;
    };

    struct RenderGraphEdge {
        RenderGraphNodeId from = 0;
        RenderGraphNodeId to = 0;
    };

    struct RenderGraphDiagnostics {
        std::uint32_t nodes = 0;
        std::uint32_t edges = 0;
        std::uint32_t executed = 0;
        std::uint32_t cycles = 0;
        std::uint32_t missing_cameras = 0;
        std::uint32_t skipped_invalid = 0;

        void begin_frame() {
            nodes = 0;
            edges = 0;
            executed = 0;
            cycles = 0;
            missing_cameras = 0;
            skipped_invalid = 0;
        }
    };

    struct RenderGraphResource {
        std::vector<RenderGraphNode> nodes;
        std::vector<RenderGraphEdge> edges;
        std::vector<RenderGraphNodeId> execution_order;
        std::unordered_map<RenderGraphNodeId, std::size_t> index;
        std::unordered_map<entt::entity, RenderGraphNodeId> camera_nodes;
        RenderGraphDiagnostics diagnostics;
        bool dirty = true;

        RenderGraphNode* find(RenderGraphNodeId id) {
            auto it = index.find(id);
            return it == index.end() ? nullptr : &nodes[it->second];
        }

        const RenderGraphNode* find(RenderGraphNodeId id) const {
            auto it = index.find(id);
            return it == index.end() ? nullptr : &nodes[it->second];
        }

        void rebuild_index() {
            index.clear();
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                index[nodes[i].id] = i;
            }
        }

        void add_edge(RenderGraphNodeId from, RenderGraphNodeId to) {
            for (const auto& edge : edges) {
                if (edge.from == from && edge.to == to) return;
            }
            edges.push_back(RenderGraphEdge{ from, to });
            dirty = true;
        }

        void remove_edges_for(RenderGraphNodeId id) {
            edges.erase(std::remove_if(edges.begin(), edges.end(),
                [&](const RenderGraphEdge& edge) {
                    return edge.from == id || edge.to == id;
                }), edges.end());
            dirty = true;
        }
    };

    inline RenderGraphNodeId render_graph_camera_node_id(entt::entity e) {
        return static_cast<RenderGraphNodeId>(entt::to_integral(e)) + 1ull;
    }
}
