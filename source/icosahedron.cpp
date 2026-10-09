#include "icosahedron.hpp"
#include <cmath>

namespace icosahedron {

std::array<Vertex, 12> createVertices() {
    // Золотое сечение. Эти три группы дают 12 вершин правильного икосаэдра.
    const float phi = (1 + std::sqrt(5.f)) / 2;
    std::array<math::Vec3, 12> points = {
        math::Vec3{-1,  phi, 0}, {1,  phi, 0}, {-1, -phi, 0}, {1, -phi, 0},
        {0, -1,  phi}, {0, 1,  phi}, {0, -1, -phi}, {0, 1, -phi},
        {phi, 0, -1}, {phi, 0, 1}, {-phi, 0, -1}, {-phi, 0, 1}
    };
    // Радиус описанной сферы равен 1; цвет получается из локальной позиции.
    std::array<Vertex, 12> vertices{};
    for (size_t i = 0; i < points.size(); ++i) {
        math::Vec3 position = math::normalized(points[i]);
        math::Vec3 color{.5f * (position.x + 1), .5f * (position.y + 1), .5f * (position.z + 1)};
        vertices[i] = {position, color};
    }
    return vertices;
}

} // namespace icosahedron
