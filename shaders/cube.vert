#version 450

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    float time;
} camera;

layout(set = 1, binding = 0) uniform ModelUBO {
    mat4 model;
} scene;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = camera.proj * camera.view * scene.model * vec4(inPosition, 1.0);
    fragColor = inColor;
}
