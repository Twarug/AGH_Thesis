#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    float time;
    float uvYStart;
    float uvYEnd;
} camera;

layout(push_constant) uniform PushConstants {
    int iterations;
    float power;
    float padding[2];
} pc;

void main()
{
    float t = camera.time * 0.5;
    vec2 globalUV = vec2(fragUV.x, mix(camera.uvYStart, camera.uvYEnd, fragUV.y));
    vec2 grid = fract(globalUV * 20.0);

    float line = step(0.9, grid.x) + step(0.9, grid.y);
    if (line < 0.5)
        discard;

    vec3 color = vec3(
        0.3 + 0.15 * sin(t),
        0.3 + 0.15 * sin(t + 2.094),
        0.35 + 0.15 * sin(t + 4.188)
    );

    outColor = vec4(color, 1.0);
}
