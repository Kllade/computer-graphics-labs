#pragma once
#include <cmath>

// Только линейная алгебра: здесь нет фигуры, ImGui или Vulkan-ресурсов.
namespace math {
constexpr float pi = 3.14159265358979323846f;
inline float radians(float degrees) {
    return degrees * pi / 180.f;
}
struct Vec3 {
    float x, y, z;
};
inline Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator*(Vec3 a, float k) {
    return {a.x * k, a.y * k, a.z * k};
}
inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 normalized(Vec3 a) {
    return a * (1.f / std::sqrt(dot(a, a)));
}
// Матрица хранится по столбцам. Вектор-столбец: clip = P * V * M * position.
struct Mat4 {
    float v[16]{};
    float &at(int row, int col) {
        return v[col * 4 + row];
    }
    float at(int row, int col) const {
        return v[col * 4 + row];
    }
};
inline Mat4 identity() {
    Mat4 m;
    for (int i = 0; i < 4; ++i)
        m.at(i, i) = 1;
    return m;
}
inline Mat4 operator*(const Mat4 &a, const Mat4 &b) {
    Mat4 c;
    for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 4; ++k)
            for (int j = 0; j < 4; ++j)
                c.at(r, j) += a.at(r, k) * b.at(k, j);
    return c;
}
inline Mat4 translation(Vec3 p) {
    auto m = identity();
    m.at(0, 3) = p.x;
    m.at(1, 3) = p.y;
    m.at(2, 3) = p.z;
    return m;
}
inline Mat4 scaling(Vec3 s) {
    auto m = identity();
    m.at(0, 0) = s.x;
    m.at(1, 1) = s.y;
    m.at(2, 2) = s.z;
    return m;
}
inline Mat4 rotation(Vec3 degrees) {
    // Сначала поворот вокруг X, затем Y, затем Z. Углы интерфейса — в градусах.
    Mat4 x = identity();
    Mat4 y = identity();
    Mat4 z = identity();
    float a = radians(degrees.x), b = radians(degrees.y), c = radians(degrees.z);
    x.at(1, 1) = x.at(2, 2) = std::cos(a);
    x.at(2, 1) = std::sin(a);
    x.at(1, 2) = -std::sin(a);
    y.at(0, 0) = y.at(2, 2) = std::cos(b);
    y.at(0, 2) = std::sin(b);
    y.at(2, 0) = -std::sin(b);
    z.at(0, 0) = z.at(1, 1) = std::cos(c);
    z.at(1, 0) = std::sin(c);
    z.at(0, 1) = -std::sin(c);
    return z * y * x;
}
inline Mat4 lookAt(Vec3 eye, Vec3 target) {
    // Базис камеры: направление взгляда, вправо, вверх.
    Vec3 f = normalized(target - eye);
    Vec3 s = normalized(cross(f, {0, 1, 0}));
    Vec3 u = cross(s, f);
    auto m = identity();
    m.at(0, 0) = s.x;
    m.at(0, 1) = s.y;
    m.at(0, 2) = s.z;
    m.at(0, 3) = -dot(s, eye);
    m.at(1, 0) = u.x;
    m.at(1, 1) = u.y;
    m.at(1, 2) = u.z;
    m.at(1, 3) = -dot(u, eye);
    m.at(2, 0) = -f.x;
    m.at(2, 1) = -f.y;
    m.at(2, 2) = -f.z;
    m.at(2, 3) = dot(f, eye);
    return m;
}
// Камера смотрит вдоль -Z. Vulkan: глубина [0,1], Y инвертирован для viewport с положительной высотой.
inline Mat4 perspective(float fov, float aspect, float nearPlane, float farPlane) {
    Mat4 m;
    float focalScale = 1 / std::tan(radians(fov) / 2);
    m.at(0, 0) = focalScale / aspect;
    m.at(1, 1) = -focalScale;
    m.at(2, 2) = farPlane / (nearPlane - farPlane);
    m.at(2, 3) = farPlane * nearPlane / (nearPlane - farPlane);
    m.at(3, 2) = -1;
    return m;
}
inline Mat4 orthographic(float height, float aspect, float nearPlane, float farPlane) {
    auto m = identity();
    m.at(0, 0) = 2 / (height * aspect);
    m.at(1, 1) = -2 / height;
    m.at(2, 2) = 1 / (nearPlane - farPlane);
    m.at(2, 3) = nearPlane / (nearPlane - farPlane);
    return m;
}
} // namespace math
