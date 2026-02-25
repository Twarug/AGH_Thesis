#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    float time;
} camera;

layout(push_constant) uniform PushConstants {
    int iterations;      // Ray march iterations
    float power;         // Mandelbulb power (typically 8)
    float padding[2];
} pc;

float mandelbulbDE(vec3 pos)
{
    vec3 z = pos;
    float dr = 1.0;
    float r = 0.0;

    float p = pc.power + sin(camera.time * 0.3) * 0.5;

    for (int i = 0; i < 15; i++)
    {
        r = length(z);
        if (r > 2.0) break;

        float theta = acos(z.z / r);
        float phi = atan(z.y, z.x);
        dr = pow(r, p - 1.0) * p * dr + 1.0;

        float zr = pow(r, p);
        theta = theta * p;
        phi = phi * p;

        z = zr * vec3(
            sin(theta) * cos(phi),
            sin(phi) * sin(theta),
            cos(theta)
        );
        z += pos;
    }

    return 0.5 * log(r) * r / dr;
}

float sceneSDF(vec3 p)
{
    // Rotate the scene over time
    float angle = camera.time * 0.2;
    float c = cos(angle);
    float s = sin(angle);
    vec3 rotatedP = vec3(
        p.x * c - p.z * s,
        p.y,
        p.x * s + p.z * c
    );

    return mandelbulbDE(rotatedP);
}

vec3 calcNormal(vec3 p)
{
    const float eps = 0.001;
    const vec2 h = vec2(eps, 0);
    return normalize(vec3(
        sceneSDF(p + h.xyy) - sceneSDF(p - h.xyy),
        sceneSDF(p + h.yxy) - sceneSDF(p - h.yxy),
        sceneSDF(p + h.yyx) - sceneSDF(p - h.yyx)
    ));
}

float calcAO(vec3 pos, vec3 nor)
{
    float occ = 0.0;
    float sca = 1.0;
    for (int i = 0; i < 5; i++)
    {
        float h = 0.01 + 0.12 * float(i) / 4.0;
        float d = sceneSDF(pos + h * nor);
        occ += (h - d) * sca;
        sca *= 0.95;
    }
    return clamp(1.0 - 3.0 * occ, 0.0, 1.0);
}

float calcSoftShadow(vec3 ro, vec3 rd, float mint, float maxt, float k)
{
    float res = 1.0;
    float t = mint;
    for (int i = 0; i < 32; i++)
    {
        if (t > maxt) break;
        float h = sceneSDF(ro + rd * t);
        if (h < 0.001)
            return 0.0;
        res = min(res, k * h / t);
        t += h;
    }
    return res;
}

void main()
{
    vec2 uv = fragUV * 2.0 - 1.0;
    uv.x *= 16.0 / 9.0;

    float camDist = 2.5 + sin(camera.time * 0.1) * 0.5;
    float camAngle = camera.time * 0.15;
    vec3 ro = vec3(
        camDist * sin(camAngle),
        0.5 + sin(camera.time * 0.2) * 0.3,
        camDist * cos(camAngle)
    );
    vec3 target = vec3(0.0);

    vec3 forward = normalize(target - ro);
    vec3 right = normalize(cross(vec3(0, 1, 0), forward));
    vec3 up = cross(forward, right);

    vec3 rd = normalize(forward + uv.x * right + uv.y * up);

    float t = 0.0;
    float tmax = 10.0;
    vec3 col = vec3(0.0);

    for (int i = 0; i < pc.iterations; i++)
    {
        vec3 p = ro + rd * t;
        float d = sceneSDF(p);

        if (d < 0.0001)
        {
            vec3 n = calcNormal(p);

            vec3 lightDir1 = normalize(vec3(1.0, 1.0, 0.5));
            vec3 lightDir2 = normalize(vec3(-0.5, 0.3, -1.0));
            vec3 lightCol1 = vec3(1.0, 0.9, 0.8);
            vec3 lightCol2 = vec3(0.4, 0.5, 0.7);

            float diff1 = max(dot(n, lightDir1), 0.0);
            float diff2 = max(dot(n, lightDir2), 0.0);

            vec3 viewDir = -rd;
            vec3 halfDir1 = normalize(lightDir1 + viewDir);
            vec3 halfDir2 = normalize(lightDir2 + viewDir);
            float spec1 = pow(max(dot(n, halfDir1), 0.0), 32.0);
            float spec2 = pow(max(dot(n, halfDir2), 0.0), 32.0);

            float ao = calcAO(p, n);

            float shadow1 = calcSoftShadow(p + n * 0.01, lightDir1, 0.01, 2.0, 16.0);
            float shadow2 = calcSoftShadow(p + n * 0.01, lightDir2, 0.01, 2.0, 16.0);

            vec3 baseColor = 0.5 + 0.5 * cos(vec3(0.0, 0.5, 1.0) + p.y * 2.0 + camera.time * 0.2);

            col = baseColor * ao * (
                lightCol1 * (diff1 * shadow1 + spec1 * shadow1 * 0.5) +
                lightCol2 * (diff2 * shadow2 + spec2 * shadow2 * 0.3)
            );

            float fresnel = pow(1.0 - max(dot(n, viewDir), 0.0), 3.0);
            col += vec3(0.3, 0.4, 0.5) * fresnel * ao;

            break;
        }

        if (t > tmax) break;
        t += d;
    }

    if (t >= tmax)
    {
        col = mix(vec3(0.1, 0.1, 0.15), vec3(0.02, 0.02, 0.05), fragUV.y);
    }

    col = col / (col + vec3(1.0));

    col = pow(col, vec3(1.0 / 2.2));

    float vignette = 1.0 - 0.3 * length(fragUV - 0.5);
    col *= vignette;

    outColor = vec4(col, 1.0);
}
