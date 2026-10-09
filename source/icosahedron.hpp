#pragma once
#include "math.hpp"
#include <array>
#include <cstdint>

// Исходная модель фигуры: геометрия и цвета. Преобразования экземпляров — в scene.hpp.
namespace icosahedron {

struct Vertex {
    math::Vec3 position; // корды x y z
    math::Vec3 color; // компоненты RGB
};

std::array<Vertex, 12> createVertices();

// Каждая строка — одна треугольная грань. Обход направлен наружу.
inline constexpr std::array<uint16_t, 60> indices = {
    0, 11, 5,
    0, 5, 1,
    0, 1, 7,
    0, 7, 10,
    0, 10, 11,
    1, 5, 9,
    5, 11, 4,
    11, 10, 2,
    10, 7, 6,
    7, 1, 8,
    3, 9, 4,
    3, 4, 2,
    3, 2, 6,
    3, 6, 8,
    3, 8, 9,
    4, 9, 5,
    2, 4, 11,
    6, 2, 10,
    8, 6, 7,
    9, 8, 1,
};

} // namespace icosahedron
