#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) flat in int fragInstanceID;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    float time;
    float uvYStart;
    float uvYEnd;
} camera;

layout(location = 0) out vec4 outColor;

float hash(float n) {
    return fract(sin(n) * 43758.5453123);
}

float hash3(vec3 p) {
    return fract(sin(dot(p, vec3(12.9898, 78.233, 45.164))) * 43758.5453);
}

float noise3D(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f); // Smoothstep

    float n = i.x + i.y * 157.0 + 113.0 * i.z;
    return mix(
        mix(mix(hash(n + 0.0), hash(n + 1.0), f.x),
            mix(hash(n + 157.0), hash(n + 158.0), f.x), f.y),
        mix(mix(hash(n + 113.0), hash(n + 114.0), f.x),
            mix(hash(n + 270.0), hash(n + 271.0), f.x), f.y), f.z);
}

float fbm(vec3 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise3D(p * frequency);
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return value;
}

vec3 calculateLighting(vec3 normal, vec3 viewDir, vec3 baseColor) {
    vec3 result = vec3(0.0);

    vec3 ambient = 0.1 * baseColor;
    result += ambient;

    const int NUM_LIGHTS = 8;
    for (int i = 0; i < NUM_LIGHTS; i++) {
        float fi = float(i);

        vec3 lightPos = vec3(
            sin(camera.time * 0.5 + fi * 0.7) * 30.0,
            cos(camera.time * 0.3 + fi * 1.1) * 20.0 + 10.0,
            cos(camera.time * 0.4 + fi * 0.9) * 30.0
        );

        vec3 lightColor = vec3(
            0.5 + 0.5 * sin(fi * 1.1),
            0.5 + 0.5 * sin(fi * 1.3 + 2.094),
            0.5 + 0.5 * sin(fi * 1.5 + 4.188)
        );

        vec3 lightDir = normalize(lightPos - fragWorldPos);
        float distance = length(lightPos - fragWorldPos);
        float attenuation = 1.0 / (1.0 + 0.01 * distance + 0.001 * distance * distance);

        float diff = max(dot(normal, lightDir), 0.0);
        vec3 diffuse = diff * baseColor * lightColor;

        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(normal, halfwayDir), 0.0), 64.0);
        vec3 specular = spec * lightColor * 0.5;

        float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0);
        vec3 fresnelColor = lightColor * fresnel * 0.3;

        result += (diffuse + specular + fresnelColor) * attenuation;
    }

    return result;
}

vec3 subsurfaceScatter(vec3 normal, vec3 viewDir, vec3 baseColor) {
    vec3 scatterColor = baseColor * vec3(1.2, 0.8, 0.6);
    float scatter = pow(1.0 - abs(dot(normal, viewDir)), 2.0);
    return scatterColor * scatter * 0.2;
}

void main() {
    vec3 normal = normalize(fragNormal);

    mat4 invView = inverse(camera.view);
    vec3 cameraPos = vec3(invView[3]);
    vec3 viewDir = normalize(cameraPos - fragWorldPos);

    float noiseScale = 5.0;
    float noiseValue = fbm(fragWorldPos * noiseScale + camera.time * 0.1, 6);

    vec3 perturbedNormal = normalize(normal + vec3(
        noise3D(fragWorldPos * 10.0 + camera.time) - 0.5,
        noise3D(fragWorldPos * 10.0 + 100.0 + camera.time) - 0.5,
        noise3D(fragWorldPos * 10.0 + 200.0 + camera.time) - 0.5
    ) * 0.1);

    vec3 baseColor = fragColor * (0.8 + noiseValue * 0.4);

    vec3 litColor = calculateLighting(perturbedNormal, viewDir, baseColor);

    litColor += subsurfaceScatter(perturbedNormal, viewDir, baseColor);

    float rim = 1.0 - max(dot(viewDir, normal), 0.0);
    rim = pow(rim, 4.0);
    vec3 rimColor = vec3(0.3, 0.5, 0.7) * rim;
    litColor += rimColor;

    float instanceVariation = hash(float(fragInstanceID) * 0.001);
    litColor *= 0.9 + instanceVariation * 0.2;

    litColor = litColor / (litColor + vec3(1.0));

    litColor = pow(litColor, vec3(1.0 / 2.2));

    outColor = vec4(litColor, 1.0);
}
