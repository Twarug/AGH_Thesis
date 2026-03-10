#version 450
#extension GL_ARB_shader_draw_parameters : enable

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    float time;
    float uvYStart;
    float uvYEnd;
} camera;

layout(std430, set = 1, binding = 0) readonly buffer ModelMatrices {
    mat4 models[];
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) flat out int fragInstanceID;

mat4 rotateAxis(vec3 axis, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    float t = 1.0 - c;
    vec3 n = normalize(axis);

    return mat4(
        t*n.x*n.x + c,      t*n.x*n.y - s*n.z,  t*n.x*n.z + s*n.y,  0,
        t*n.x*n.y + s*n.z,  t*n.y*n.y + c,      t*n.y*n.z - s*n.x,  0,
        t*n.x*n.z - s*n.y,  t*n.y*n.z + s*n.x,  t*n.z*n.z + c,      0,
        0,                   0,                   0,                   1
    );
}

float hash(float n) {
    return fract(sin(n) * 43758.5453123);
}

vec3 noise3(float seed) {
    return vec3(
        hash(seed),
        hash(seed + 127.1),
        hash(seed + 269.5)
    ) * 2.0 - 1.0;
}

void main() {
    int id = gl_DrawIDARB;
    mat4 baseModel = models[id];

    float idFloat = float(id);
    vec3 rotAxis = normalize(noise3(idFloat * 0.1) + vec3(0.001)); // Unique rotation axis
    float rotSpeed = 0.5 + hash(idFloat * 0.2) * 1.5; // Varied rotation speed
    float phase = hash(idFloat * 0.3) * 6.28318; // Random phase offset

    float angle1 = camera.time * rotSpeed + phase;
    mat4 animRotation1 = rotateAxis(rotAxis, angle1);

    vec3 wobbleAxis = normalize(vec3(sin(idFloat), cos(idFloat * 1.3), sin(idFloat * 0.7)));
    float wobbleAngle = sin(camera.time * 2.0 + phase) * 0.3;
    mat4 animRotation2 = rotateAxis(wobbleAxis, wobbleAngle);

    float scaleBase = 0.8 + hash(idFloat * 0.4) * 0.4;
    float scalePulse = 1.0 + sin(camera.time * 3.0 + phase * 2.0) * 0.1;
    float finalScale = scaleBase * scalePulse;
    mat4 scaleMatrix = mat4(
        finalScale, 0, 0, 0,
        0, finalScale, 0, 0,
        0, 0, finalScale, 0,
        0, 0, 0, 1
    );

    vec3 basePosition = vec3(baseModel[3]);
    float orbitRadius = 0.5 + hash(idFloat * 0.5) * 0.5;
    float orbitSpeed = 0.3 + hash(idFloat * 0.6) * 0.4;
    vec3 orbitOffset = vec3(
        cos(camera.time * orbitSpeed + phase) * orbitRadius,
        sin(camera.time * orbitSpeed * 1.3 + phase) * orbitRadius * 0.5,
        sin(camera.time * orbitSpeed + phase) * orbitRadius
    );

    mat4 translation = mat4(1.0);
    translation[3] = vec4(basePosition + orbitOffset, 1.0);

    mat4 localTransform = baseModel;
    localTransform[3] = vec4(0, 0, 0, 1);

    mat4 model = translation * animRotation1 * animRotation2 * scaleMatrix * localTransform;

    vec3 pos = inPosition;
    float displacement = sin(pos.x * 10.0 + camera.time) *
                        cos(pos.y * 10.0 + camera.time * 1.1) *
                        sin(pos.z * 10.0 + camera.time * 0.9) * 0.02;
    pos += normalize(pos) * displacement;

    vec4 worldPos = model * vec4(pos, 1.0);
    gl_Position = camera.proj * camera.view * worldPos;

    fragWorldPos = worldPos.xyz;

    mat3 normalMatrix = mat3(animRotation1 * animRotation2);
    vec3 localNormal = normalize(inPosition); // Sphere normal
    fragNormal = normalMatrix * localNormal;

    fragInstanceID = id;

    float hue = fract(idFloat * 0.618033988749895 + camera.time * 0.1);
    float saturation = 0.7 + sin(camera.time + phase) * 0.3;
    float brightness = 0.6 + cos(camera.time * 1.5 + phase * 2.0) * 0.2;

    vec3 hsv = vec3(hue, saturation, brightness);
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(hsv.xxx + K.xyz) * 6.0 - K.www);
    fragColor = hsv.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), hsv.y);
}
