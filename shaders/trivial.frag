#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    float time;
} camera;

layout(push_constant) uniform PushConstants {
    int iterations;
    float power;
    float padding[2];
} pc;

void main()
{
    float t = camera.time * 0.5;
    vec3 color = vec3(
        0.1 + 0.05 * sin(t),
        0.1 + 0.05 * sin(t + 2.094),
        0.15 + 0.05 * sin(t + 4.188)
    );

    vec2 grid = fract(fragUV * 20.0);
    float line = step(0.95, grid.x) + step(0.95, grid.y);
    color += vec3(0.02) * line;

    outColor = vec4(color, 1.0);
}
