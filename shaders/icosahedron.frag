#version 450

layout(std140, set = 0, binding = 0) uniform ObjectUniforms {
    mat4 mvp;
    mat4 model;
    vec4 baseColor;
    float useVertexColors;
    float enableShading;
    vec2 padding;
} object;

// Эти значения интерполированы по треугольнику после vertex shader.
layout(location = 0) in vec3 color;
layout(location = 1) in vec3 worldPosition;
layout(location = 0) out vec4 outputColor;

void main() {
    vec3 finalColor = color;

    if (object.enableShading > 0.5) {
        // Производные мировой позиции дают плоскую нормаль треугольной грани.
        vec3 normal = normalize(cross(dFdx(worldPosition), dFdy(worldPosition)));
        vec3 lightDirection = normalize(vec3(0.4, 0.7, 1.0));
        // Простая двусторонняя подсветка. Это необязательная часть первой лабы.
        const float ambient = 0.35;
        const float directional = 0.65;
        float brightness = ambient + directional * abs(dot(normal, lightDirection));
        finalColor *= brightness;
    }

    outputColor = vec4(finalColor, 1.0);
}
