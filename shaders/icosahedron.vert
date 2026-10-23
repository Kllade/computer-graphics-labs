#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(set = 0, binding = 0, std140) uniform Scene {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 albedo;
    vec4 specular;
    vec4 ambient;
} scene;
layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;

// преобразование позиции и передача нормали
void main() {
    gl_Position = scene.viewProjection * vec4(position, 1.0);
    worldPosition = position; // матрица модели единичная
    worldNormal = normal;
}
