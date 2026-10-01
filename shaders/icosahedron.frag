#version 450
layout(set=0,binding=0) uniform ObjectData {
    mat4 mvp;
    mat4 model;
    vec4 tint;
    vec4 options;
} object;
layout(location=0) in vec3 color;
layout(location=1) in vec3 worldPosition;
layout(location=0) out vec4 outputColor;
void main() {
    vec3 normal=normalize(cross(dFdx(worldPosition),dFdy(worldPosition)));
    float light=.35+.65*abs(dot(normal,normalize(vec3(.4,.7,1))));
    outputColor=vec4(color*mix(1,light,object.options.y),1);
}
