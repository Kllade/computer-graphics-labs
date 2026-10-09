#version 450

// Формат вершины совпадает с icosahedron::Vertex.
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 vertexColor;

// Порядок полей совпадает с ObjectUniforms в application.cpp (160 байт, std140).
layout(std140, set = 0, binding = 0) uniform ObjectUniforms {
    mat4 mvp;
    mat4 model;
    vec4 baseColor;
    float useVertexColors;
    float enableShading;
    vec2 padding;
} object;

layout(location = 0) out vec3 color;
layout(location = 1) out vec3 worldPosition;

void main() {
    // Этап 1: локальная позиция -> clip space. Затем GPU выполнит деление на w.
    gl_Position = object.mvp * vec4(position, 1.0);

    // Этап 2: мировая позиция нужна фрагментному шейдеру для нормали грани.
    worldPosition = (object.model * vec4(position, 1.0)).xyz;

    // Этап 3: однотонная фигура или процедурный цвет вершины, умноженный на цвет UI.
    color = object.baseColor.rgb;
    if (object.useVertexColors > 0.5)
        color *= vertexColor;
}
