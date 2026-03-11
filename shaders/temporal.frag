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

layout(set = 1, binding = 0) uniform sampler2D previousFrame;

layout(push_constant) uniform PushConstants {
    int numBalls;
} pc;

void main()
{
    // Global UV for ball positions (continuous across SFR strips)
    vec2 uv = vec2(fragUV.x, mix(camera.uvYStart, camera.uvYEnd, fragUV.y));

    // Local UV for sampling previousFrame (covers only this GPU's strip)
    vec2 localUV = fragUV;

    // Fade previous frame toward black
    vec3 prevColor = texture(previousFrame, localUV).rgb;

    // Draw balls at current positions
    vec3 newColor = vec3(0.0);

    int numBalls = 10; // pc.numBalls;
    for (int i = 0; i < numBalls; i++)
    {
        float fi = float(i) / float(numBalls);

        float orbitSpeed  = 0.5 + fi * 1.1;
        float orbitRadius = 0.12 + 0.18 * sin(fi * 3.7 + 1.0);
        vec2 center = vec2(
            0.5 + 0.18 * cos(camera.time * 0.17 + fi * 2.39),
            0.5 + 0.18 * sin(camera.time * 0.13 + fi * 1.91)
        );
        float angle = camera.time * orbitSpeed + fi * 6.28318;
        vec2 pos = center + vec2(cos(angle), sin(angle)) * orbitRadius;

        float dist = length(uv - pos);
        float ball = smoothstep(0.030, 0.029, dist);

        vec3 color = 0.5 + 0.5 * cos(fi * 6.28318 + vec3(0.0, 2.094, 4.188));
        newColor += color * ball;
    }

    vec3 finalColor = length(newColor) > .001 ? newColor : prevColor;

    outColor = vec4(finalColor, 1.0);
}
