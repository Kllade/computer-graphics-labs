#pragma once
#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <cstdint>

// икосаэдр с центром в нуле и радиусом 1
namespace icosahedron {
inline constexpr float a = 0.5257311f, b = 0.8506508f;
inline const std::array<glm::vec3, 12> vertices = {{
    {-a, b, 0}, {a, b, 0}, {-a, -b, 0}, {a, -b, 0},
    {0, -a, b}, {0, a, b}, {0, -a, -b}, {0, a, -b},
    {b, 0, -a}, {b, 0, a}, {-b, 0, -a}, {-b, 0, a}
}};

// тройки индексов граней, обход наружу
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

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv; // координаты на изображении
};

// 60 вершин: у каждой грани своя нормаль
inline std::array<Vertex, 60> makeVertices() {
    std::array<Vertex, 60> result{};
    for (std::size_t i = 0; i < indices.size(); i += 3) {
        const auto p0 = vertices[indices[i]];
        const auto p1 = vertices[indices[i + 1]];
        const auto p2 = vertices[indices[i + 2]];
        // единичная нормаль через векторное произведение рёбер
        const auto normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));
        // один и тот же участок текстуры на каждой треугольной грани
        result[i] = {p0, normal, {0, 0}};
        result[i + 1] = {p1, normal, {1, 0}};
        result[i + 2] = {p2, normal, {0.5f, 1}};
    }
    return result;
}
} // namespace icosahedron
