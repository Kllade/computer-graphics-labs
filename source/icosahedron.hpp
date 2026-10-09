#pragma once
#include <glm/vec3.hpp>
#include <array>
#include <cstdint>

// Правильный икосаэдр, центр (0, 0, 0), радиус описанной сферы 1.
namespace icosahedron {
// Нормализованные координаты: b / a = (1 + sqrt(5)) / 2 — золотое сечение.
inline constexpr float a = 0.5257311f, b = 0.8506508f;
inline const std::array<glm::vec3, 12> vertices = {{
    {-a, b, 0}, {a, b, 0}, {-a, -b, 0}, {a, -b, 0},
    {0, -a, b}, {0, a, b}, {0, -a, -b}, {0, a, -b},
    {b, 0, -a}, {b, 0, a}, {-b, 0, -a}, {-b, 0, a}
}};

// Каждая строка — одна грань (три номера вершин), обход наружу.
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
