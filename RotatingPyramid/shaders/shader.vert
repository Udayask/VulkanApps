#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
} objTransform;

layout(push_constant) uniform PushConstant {
    mat4 viewProj;
} tform;

layout(location = 0) in  vec3 inPosition;
layout(location = 1) in  vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = tform.viewProj * objTransform.model * vec4(inPosition, 1.0);
    fragColor   = inColor;
}
