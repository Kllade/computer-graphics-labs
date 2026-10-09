#version 450
layout(location = 0) in vec3 position;
layout(push_constant) uniform Transform {
    mat4 mvp;
} transform;
layout(location = 0) out vec3 worldPosition;

void main() {
    gl_Position = transform.mvp * vec4(position, 1.0);
    worldPosition = position; // Матрица модели единичная: локальная позиция совпадает с мировой.
}
