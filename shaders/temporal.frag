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
    int iterations;      // Processing iterations per pixel
    float decayRate;     // How much previous frame fades
    float padding[2];
} pc;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p, int octaves)
{
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++)
    {
        value += amplitude * noise(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value;
}

void main()
{
    // Global UV for procedural patterns (continuous across SFR strips)
    vec2 uv = vec2(fragUV.x, mix(camera.uvYStart, camera.uvYEnd, fragUV.y));

    // Local UV for previousFrame texture sampling (texture covers this GPU's section)
    vec2 localUV = fragUV;
    vec2 distortedUV = localUV;
    float distortAmount = 0.002;
    distortedUV.x += sin(uv.y * 20.0 + camera.time * 2.0) * distortAmount;
    distortedUV.y += cos(uv.x * 20.0 + camera.time * 2.0) * distortAmount;

    distortedUV = clamp(distortedUV, 0.001, 0.999);

    vec3 prevColor = texture(previousFrame, distortedUV).rgb;

    prevColor *= pc.decayRate;

    vec3 newColor = vec3(0.0);

    for (int i = 0; i < pc.iterations; i++)
    {
        float fi = float(i) / float(pc.iterations);

        vec2 p = uv * 10.0 + camera.time * 0.5 + fi * 0.1;

        float prevLum = dot(prevColor, vec3(0.299, 0.587, 0.114));

        float pattern = fbm(p + prevLum * 2.0, 4);
        pattern += 0.5 * fbm(p * 2.0 - camera.time * 0.3, 3);

        vec3 iterColor;
        iterColor.r = pattern * (0.5 + 0.5 * sin(camera.time + fi * 6.28));
        iterColor.g = pattern * (0.5 + 0.5 * sin(camera.time + fi * 6.28 + 2.094));
        iterColor.b = pattern * (0.5 + 0.5 * sin(camera.time + fi * 6.28 + 4.188));

        newColor += iterColor;
    }

    newColor /= float(pc.iterations);

    float emitterCount = 5.0;
    for (float e = 0.0; e < emitterCount; e++)
    {
        float angle = camera.time * (0.5 + e * 0.1) + e * 1.257;
        float radius = 0.3 + 0.1 * sin(camera.time * 2.0 + e);
        vec2 emitterPos = vec2(0.5) + vec2(cos(angle), sin(angle)) * radius;

        float dist = length(uv - emitterPos);
        float emitter = smoothstep(0.05, 0.0, dist);

        vec3 emitterColor = vec3(
            0.5 + 0.5 * sin(e * 1.1 + camera.time),
            0.5 + 0.5 * sin(e * 1.3 + camera.time + 2.094),
            0.5 + 0.5 * sin(e * 1.5 + camera.time + 4.188)
        );

        newColor += emitterColor * emitter * 2.0;
    }

    vec3 finalColor = prevColor + newColor * 0.3;

    finalColor = finalColor / (finalColor + vec3(1.0));

    finalColor = pow(finalColor, vec3(1.0 / 2.2));

    outColor = vec4(finalColor, 1.0);
}
