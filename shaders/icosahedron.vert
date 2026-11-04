#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(set = 0, binding = 0, std140) uniform Scene {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 specular;
    vec4 ambient;
} scene;
layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 textureUV;

// преобразование позиции и передача нормали
void main() {
    gl_Position = scene.viewProjection * vec4(position, 1.0);
    worldPosition = position; // матрица модели единичная
    worldNormal = normal;
    textureUV = uv; // GPU интерполирует UV между вершинами треугольника
}
