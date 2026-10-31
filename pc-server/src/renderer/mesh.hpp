#pragma once

#include <cstdint>
#include <vector>

namespace argb::renderer {

struct Vertex {
    float position[3];
    float color[3];
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint16_t> indices;
};

MeshData makeCubeMesh();

} // namespace argb::renderer
