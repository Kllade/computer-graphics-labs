#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 vertexColor;
layout(set=0,binding=0) uniform ObjectData {
    mat4 mvp;
    mat4 model;
    vec4 tint;
    vec4 options;
} object;
layout(location=0) out vec3 color;
layout(location=1) out vec3 worldPosition;
void main() {
    gl_Position=object.mvp*vec4(position,1);
    worldPosition=(object.model*vec4(position,1)).xyz;
    color=mix(vec3(1),vertexColor,object.options.x)*object.tint.rgb;
}
