#version 450
layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(set = 0, binding = 0, std140) uniform Scene {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 albedo;
    vec4 specular;
    vec4 ambient;
} scene;
layout(location = 0) out vec4 outputColor;

void main() {
    vec3 N = normalize(worldNormal);
    vec3 L = normalize(scene.lightDirection.xyz); // Направление от поверхности к свету.
    vec3 toCamera = scene.cameraPosition.xyz - worldPosition;
    vec3 V = toCamera / max(length(toCamera), 0.0001);
    vec3 H = (L + V) / max(length(L + V), 0.0001);

    float diffuse = max(dot(N, L), 0.0);
    float specular = 0.0;
    if (diffuse > 0.0)
        specular = pow(max(dot(N, H), 0.0), scene.specular.w);

    vec3 ambient = scene.ambient.rgb * scene.ambient.w * scene.albedo.rgb;
    vec3 direct = scene.lightColor.rgb * scene.lightColor.w
                * (scene.albedo.rgb * diffuse + scene.specular.rgb * specular);
    outputColor = vec4(ambient + direct, 1.0);
}
