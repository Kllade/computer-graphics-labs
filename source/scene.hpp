#pragma once
#include "math.hpp"
#include <algorithm>

// Только настройки CPU и вычисление матриц. Геометрия — в icosahedron.cpp.
namespace scene {

struct ObjectState {
    math::Vec3 position{0, 0, 0};
    math::Vec3 rotationDegrees{15, 25, 0};
    math::Vec3 scale{1, 1, 1};
    // Цвет и необязательные режимы шейдера.
    float tint[3]{1, 1, 1};
    bool vertexColors = true;
    bool lighting = true;
    // Время накапливается только во время воспроизведения.
    bool playing = false;
    float speed = 1;
    float radius = 1.2f;
    float height = .6f;
    float spin = 35; // Базовая скорость вращения: градусы на секунду фазы.
    double animationTime = 0;
};

inline void updateAnimation(ObjectState &object, double deltaTime) {
    // Пауза сохраняет фазу. Ограничение шага убирает скачок после долгого кадра.
    if (object.playing)
        object.animationTime += std::clamp(deltaTime, 0.0, .1) * object.speed;
}

inline math::Vec3 trajectoryOffset(const ObjectState &object) {
    float t = static_cast<float>(object.animationTime);
    return {object.radius * std::sin(t),
            object.height * std::sin(2 * t),
            .6f * object.radius * (std::cos(t) - 1)};
}

inline math::Mat4 modelMatrix(const ObjectState &object) {
    double angle = object.animationTime * object.spin;
    // Каждая ось замыкает свой угол отдельно: нет скачка X/Z при angle=360.
    math::Vec3 animationRotation{
        static_cast<float>(std::fmod(.6 * angle, 360.0)),
        static_cast<float>(std::fmod(angle, 360.0)),
        static_cast<float>(std::fmod(.25 * angle, 360.0))};
    // Для вектора-столбца справа налево: масштаб -> X/Y/Z повороты -> перенос.
    math::Mat4 model = math::translation(object.position + trajectoryOffset(object))
                    * math::rotation(object.rotationDegrees + animationRotation)
                    * math::scaling(object.scale);
    return model;
}

// Камера и проекция общие для трёх объектов.
struct Camera {
    bool perspectiveProjection = true;
    float fov = 45;
    float orthoHeight = 6;
    float cameraDistance = 8;
    float cameraYaw = 0;
    float cameraPitch = 12;
};

inline math::Mat4 viewMatrix(const Camera &camera) {
    float yaw = math::radians(camera.cameraYaw);
    float pitch = math::radians(camera.cameraPitch);
    float distance = camera.cameraDistance;
    math::Vec3 eye{distance * std::cos(pitch) * std::sin(yaw),
                   distance * std::sin(pitch),
                   distance * std::cos(pitch) * std::cos(yaw)};
    return math::lookAt(eye, {0, 0, 0});
}

inline math::Mat4 projectionMatrix(const Camera &camera, float aspect) {
    constexpr float nearPlane = .1f;
    constexpr float farPlane = 100;
    if (camera.perspectiveProjection)
        return math::perspective(camera.fov, aspect, nearPlane, farPlane);
    return math::orthographic(camera.orthoHeight, aspect, nearPlane, farPlane);
}

} // namespace scene
