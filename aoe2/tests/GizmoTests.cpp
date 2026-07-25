#include <cassert>

#include <ecs/render/Gizmo.hpp>

using namespace gld::ecs;

int main() {
    const glm::vec4 white{1.f};
    Gizmos gizmos;
    gizmos.line({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, white, 0x1u);
    assert(gizmos.vertex_count(0x1u) == 2);
    gizmos.cross({0.f, 0.f, 0.f}, 1.f, white, 0x2u);
    assert(gizmos.vertex_count(0x2u) == 6);
    gizmos.arrow({0.f, 0.f, 0.f}, {2.f, 0.f, 0.f}, 0.5f, white, 0x4u);
    assert(gizmos.vertex_count(0x4u) == 10);
    gizmos.wire_box({0.f, 0.f, 0.f}, {1.f, 2.f, 3.f}, glm::mat3(1.f),
                    white, 0x8u);
    assert(gizmos.vertex_count(0x8u) == 24);
    gizmos.wire_ellipse({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
                        8, white, 0x10u);
    assert(gizmos.vertex_count(0x10u) == 16);
    gizmos.wire_ellipsoid({0.f, 0.f, 0.f}, {1.f, 2.f, 3.f}, glm::mat3(1.f),
                          8, white, 0x20u);
    assert(gizmos.vertex_count(0x20u) == 48);

    const auto& stream = gizmos.vertex_stream();
    const auto& ranges = gizmos.draw_ranges();
    assert(stream.size() == 106);
    assert(ranges.size() == 6);
    std::size_t visible_to_camera_0x12 = 0;
    for (const auto& range : ranges) {
        if ((range.layers & 0x12u) != 0) visible_to_camera_0x12 += range.vertex_count;
    }
    assert(visible_to_camera_0x12 == 22); // cross layer 2 + ellipse layer 16
    for (std::size_t i = 1; i < ranges.size(); ++i)
        assert(ranges[i].first_vertex ==
               ranges[i - 1].first_vertex + ranges[i - 1].vertex_count);

    gizmos.clear();
    assert(gizmos.vertex_stream().empty());
    assert(gizmos.draw_ranges().empty());
    return 0;
}
