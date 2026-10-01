#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace scene {
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
// Column-major storage, column vectors: clip = projection * view * model * position.
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
    auto x = identity(), y = x, z = x;
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
    Vec3 f = normalized(target - eye), s = normalized(cross(f, {0, 1, 0})), u = cross(s, f);
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
// Right-handed view space, Vulkan depth [0,1], inverted Y for positive-height viewport.
inline Mat4 perspective(float fov, float aspect, float nearPlane, float farPlane) {
    Mat4 m;
    float t = 1 / std::tan(radians(fov) / 2);
    m.at(0, 0) = t / aspect;
    m.at(1, 1) = -t;
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
struct Vertex {
    Vec3 position;
    Vec3 color;
};
inline std::array<Vertex, 12> vertices() {
    const float t = (1 + std::sqrt(5.f)) / 2;
    std::array<Vec3, 12> points = {Vec3{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
                                   {0, -1, t},     {0, 1, t}, {0, -1, -t}, {0, 1, -t},
                                   {t, 0, -1},     {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    std::array<Vertex, 12> out{};
    for (size_t i = 0; i < points.size(); ++i) {
        auto p = normalized(points[i]);
        out[i] = {p, {.5f * (p.x + 1), .5f * (p.y + 1), .5f * (p.z + 1)}};
    }
    return out;
}
inline constexpr std::array<uint16_t, 60> indices = {
    0, 11, 5,  0, 5,  1, 0, 1, 7, 0, 7,  10, 0, 10, 11, 1, 5, 9, 5, 11,
    4, 11, 10, 2, 10, 7, 6, 7, 1, 8, 3,  9,  4, 3,  4,  2, 3, 2, 6, 3,
    6, 8,  3,  8, 9,  4, 9, 5, 2, 4, 11, 6,  2, 10, 8,  6, 7, 9, 8, 1};
struct ObjectState {
    Vec3 position{0, 0, 0}, angles{15, 25, 0}, scale{1, 1, 1};
    float tint[3]{1, 1, 1};
    bool vertexColors = true, lighting = true, playing = false;
    float speed = 1, radius = 1.2f, height = .6f, spin = 35;
    double animationTime = 0;
};
inline void advance(ObjectState &c, double dt) {
    if (c.playing)
        c.animationTime += std::clamp(dt, 0.0, .1) * c.speed;
}
inline Vec3 trajectory(const ObjectState &c) {
    float t = static_cast<float>(c.animationTime);
    return {c.radius * std::sin(t), c.height * std::sin(2 * t), .6f * c.radius * (std::cos(t) - 1)};
}
inline Mat4 model(const ObjectState &c) {
    float a = static_cast<float>(std::fmod(c.animationTime * c.spin, 360.0));
    return translation(c.position + trajectory(c)) *
           rotation(c.angles + Vec3{.6f * a, a, .25f * a}) * scaling(c.scale);
}
// Камера и проекция общие для всех объектов.
struct Camera {
    bool perspectiveProjection = true;
    float fov = 45, orthoHeight = 6;
    float cameraDistance = 8, cameraYaw = 0, cameraPitch = 12;
};
inline Mat4 view(const Camera &c) {
    float y = radians(c.cameraYaw), p = radians(c.cameraPitch), d = c.cameraDistance;
    return lookAt({d * std::cos(p) * std::sin(y), d * std::sin(p), d * std::cos(p) * std::cos(y)},
                  {0, 0, 0});
}
inline Mat4 projection(const Camera &c, float aspect) {
    return c.perspectiveProjection ? perspective(c.fov, aspect, .1f, 100.f)
                                   : orthographic(c.orthoHeight, aspect, .1f, 100.f);
}
} // namespace scene
