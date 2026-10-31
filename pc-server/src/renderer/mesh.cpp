#include "renderer/mesh.hpp"

namespace argb::renderer {
namespace {

constexpr float kHalf = 0.5f;

void appendFace(MeshData& mesh,
                const float positions[4][3],
                const float color[3],
                std::uint16_t base) {
    for (int i = 0; i < 4; ++i) {
        Vertex vertex{};
        vertex.position[0] = positions[i][0];
        vertex.position[1] = positions[i][1];
        vertex.position[2] = positions[i][2];
        vertex.color[0] = color[0];
        vertex.color[1] = color[1];
        vertex.color[2] = color[2];
        mesh.vertices.push_back(vertex);
    }
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
}

} // namespace

MeshData makeCubeMesh() {
    MeshData mesh;

    const float frontColor[3] = {0.90f, 0.25f, 0.25f};
    const float backColor[3] = {0.20f, 0.75f, 0.30f};
    const float topColor[3] = {0.25f, 0.45f, 0.95f};
    const float bottomColor[3] = {0.95f, 0.85f, 0.20f};
    const float rightColor[3] = {0.85f, 0.30f, 0.85f};
    const float leftColor[3] = {0.20f, 0.85f, 0.85f};

    const float front[4][3] = {
        {-kHalf, -kHalf, kHalf}, {kHalf, -kHalf, kHalf},
        {kHalf, kHalf, kHalf}, {-kHalf, kHalf, kHalf}};
    const float back[4][3] = {
        {kHalf, -kHalf, -kHalf}, {-kHalf, -kHalf, -kHalf},
        {-kHalf, kHalf, -kHalf}, {kHalf, kHalf, -kHalf}};
    const float top[4][3] = {
        {-kHalf, kHalf, kHalf}, {kHalf, kHalf, kHalf},
        {kHalf, kHalf, -kHalf}, {-kHalf, kHalf, -kHalf}};
    const float bottom[4][3] = {
        {-kHalf, -kHalf, -kHalf}, {kHalf, -kHalf, -kHalf},
        {kHalf, -kHalf, kHalf}, {-kHalf, -kHalf, kHalf}};
    const float right[4][3] = {
        {kHalf, -kHalf, kHalf}, {kHalf, -kHalf, -kHalf},
        {kHalf, kHalf, -kHalf}, {kHalf, kHalf, kHalf}};
    const float left[4][3] = {
        {-kHalf, -kHalf, -kHalf}, {-kHalf, -kHalf, kHalf},
        {-kHalf, kHalf, kHalf}, {-kHalf, kHalf, -kHalf}};

    appendFace(mesh, front, frontColor, 0);
    appendFace(mesh, back, backColor, 4);
    appendFace(mesh, top, topColor, 8);
    appendFace(mesh, bottom, bottomColor, 12);
    appendFace(mesh, right, rightColor, 16);
    appendFace(mesh, left, leftColor, 20);

    return mesh;
}

} // namespace argb::renderer
