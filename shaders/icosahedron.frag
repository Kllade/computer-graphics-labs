#version 450
layout(location = 0) in vec3 worldPosition;
layout(location = 0) out vec4 outputColor;

void main() {
    // Фиксированная подсветка делает плоские грани различимыми; настроек цвета и света нет.
    vec3 normal = normalize(cross(dFdx(worldPosition), dFdy(worldPosition)));
    vec3 lightDirection = normalize(vec3(0.4, 0.7, 1.0));
    float brightness = 0.35 + 0.65 * abs(dot(normal, lightDirection));
    outputColor = vec4(vec3(0.25, 0.65, 0.9) * brightness, 1.0);
}
