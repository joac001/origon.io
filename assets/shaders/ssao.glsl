// Screen-space ambient occlusion from the depth prepass (half resolution).
#pragma stage vertex
out vec2 vUv;

void main() {
    vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vUv = positions[gl_VertexID] * 0.5 + 0.5;
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}

#pragma stage fragment
in vec2 vUv;
out float fragAo;

uniform sampler2D uDepth;
uniform mat4 uProj;
uniform mat4 uInvProj;
uniform vec2 uDepthTexel;

const int SAMPLES = 16;
const float RADIUS = 0.55;
const float BIAS = 0.025;

vec3 viewPosition(vec2 uv) {
    float depth = texture(uDepth, uv).r;
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = uInvProj * ndc;
    return view.xyz / view.w;
}

// Normal from the depth buffer, picking the smaller difference on each axis to avoid edge errors.
vec3 viewNormal(vec2 uv, vec3 center) {
    vec3 left = viewPosition(uv - vec2(uDepthTexel.x, 0.0));
    vec3 right = viewPosition(uv + vec2(uDepthTexel.x, 0.0));
    vec3 down = viewPosition(uv - vec2(0.0, uDepthTexel.y));
    vec3 up = viewPosition(uv + vec2(0.0, uDepthTexel.y));
    vec3 dx = abs(right.z - center.z) < abs(center.z - left.z) ? right - center : center - left;
    vec3 dy = abs(up.z - center.z) < abs(center.z - down.z) ? up - center : center - down;
    return normalize(cross(dx, dy));
}

float interleavedGradientNoise(vec2 p) {
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    float depth = texture(uDepth, vUv).r;
    if (depth >= 1.0) {
        fragAo = 1.0;
        return;
    }
    vec3 position = viewPosition(vUv);
    vec3 normal = viewNormal(vUv, position);

    float angle = interleavedGradientNoise(gl_FragCoord.xy) * 6.2831853;
    vec3 randomVec = vec3(cos(angle), sin(angle), 0.0);
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 tbn = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for (int i = 0; i < SAMPLES; ++i) {
        // Fibonacci hemisphere, denser near the center.
        float fi = float(i) + 0.5;
        float phi = fi * 2.39996323;
        float cosTheta = 1.0 - fi / float(SAMPLES);
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        vec3 dir = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
        float scale = mix(0.15, 1.0, (fi / float(SAMPLES)) * (fi / float(SAMPLES)));
        vec3 samplePos = position + tbn * dir * RADIUS * scale;

        vec4 clip = uProj * vec4(samplePos, 1.0);
        vec2 sampleUv = clip.xy / clip.w * 0.5 + 0.5;
        float sceneZ = viewPosition(sampleUv).z;
        float range = smoothstep(0.0, 1.0, RADIUS / max(abs(position.z - sceneZ), 1e-4));
        occlusion += (sceneZ >= samplePos.z + BIAS ? 1.0 : 0.0) * range;
    }
    float ao = 1.0 - occlusion / float(SAMPLES);
    fragAo = pow(clamp(ao, 0.0, 1.0), 1.6);
}
