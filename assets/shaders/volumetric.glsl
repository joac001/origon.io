// Volumetric sun scattering: ray march from the camera to the scene depth, sampling the
// cascaded shadow maps so light shafts appear between tree trunks and canopies.
#pragma stage vertex
out vec2 vUv;

void main() {
    vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vUv = positions[gl_VertexID] * 0.5 + 0.5;
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}

#pragma stage fragment
in vec2 vUv;
out vec3 fragColor;

const int MAX_CASCADES = 4;
const int STEPS = 28;

uniform sampler2D uDepth;
uniform sampler2DArrayShadow uShadowMap;
uniform mat4 uInvViewProj;
uniform mat4 uView;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform float uDensity;
uniform float uGroundHeight;
uniform float uMaxDistance;
uniform float uTime;
uniform int uCascadeCount;
uniform mat4 uCascadeViewProj[MAX_CASCADES];
uniform float uCascadeSplit[MAX_CASCADES];

float sunVisibility(vec3 p) {
    float viewDepth = -(uView * vec4(p, 1.0)).z;
    for (int i = 0; i < uCascadeCount; ++i) {
        if (viewDepth < uCascadeSplit[i]) {
            vec4 clip = uCascadeViewProj[i] * vec4(p, 1.0);
            vec3 coord = clip.xyz / clip.w * 0.5 + 0.5;
            if (coord.z >= 1.0) return 1.0;
            return texture(uShadowMap, vec4(coord.xy, float(i), coord.z - 0.0008));
        }
    }
    return 1.0;
}

float phaseHG(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5));
}

float noise3(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n = dot(i, vec3(1.0, 57.0, 113.0));
    vec4 h = fract(sin(vec4(n, n + 1.0, n + 57.0, n + 58.0)) * 43758.5453);
    vec4 h2 = fract(sin(vec4(n + 113.0, n + 114.0, n + 170.0, n + 171.0)) * 43758.5453);
    float a = mix(mix(h.x, h.y, f.x), mix(h.z, h.w, f.x), f.y);
    float b = mix(mix(h2.x, h2.y, f.x), mix(h2.z, h2.w, f.x), f.y);
    return mix(a, b, f.z);
}

void main() {
    float depth = texture(uDepth, vUv).r;
    vec4 farPoint = uInvViewProj * vec4(vUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec3 target = farPoint.xyz / farPoint.w;
    vec3 ray = target - uCameraPos;
    float distance = min(length(ray), uMaxDistance);
    vec3 dir = normalize(ray);
    float stepLength = distance / float(STEPS);
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float phase = phaseHG(dot(dir, uSunDirection), 0.42) * 0.65 + 0.02;

    vec3 light = vec3(0.0);
    float transmittance = 1.0;
    for (int i = 0; i < STEPS; ++i) {
        vec3 p = uCameraPos + dir * (float(i) + jitter) * stepLength;
        // Denser near the ground, drifting wisps.
        float heightFalloff = exp(-max(p.y - uGroundHeight, 0.0) * 0.06);
        float wisps = 0.6 + 0.8 * noise3(p * 0.08 + vec3(uTime * 0.15, 0.0, uTime * 0.07));
        float density = uDensity * heightFalloff * wisps;
        float scatter = density * stepLength;
        light += sunVisibility(p) * phase * scatter * transmittance;
        transmittance *= exp(-scatter);
    }
    fragColor = light * uSunColor;
}
