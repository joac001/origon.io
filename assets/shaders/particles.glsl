// Camera-facing particles (smoke, dust, chips, sparks): soft procedural puffs, blended with
// premultiplied alpha (alpha 0 = additive glow), faded where they meet geometry (soft particles).
#pragma stage vertex
struct Particle {
    vec4 positionSize; // xyz world position, w half size (meters)
    vec4 color;        // rgb albedo or glow, a opacity (0 = additive glow)
    vec4 params;       // x rotation, y seed, z glow (1 = emissive), w unused
};
layout(std430, binding = 2) readonly buffer Particles { Particle uParticles[]; };

uniform mat4 uViewProj;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;

out vec2 vUv;
out vec4 vColor;
out vec3 vWorldPos;
flat out vec3 vParams;

const vec2 kCorners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));

void main() {
    Particle p = uParticles[gl_InstanceID];
    vec2 corner = kCorners[gl_VertexID];
    float c = cos(p.params.x), s = sin(p.params.x);
    vec2 rotated = vec2(c * corner.x - s * corner.y, s * corner.x + c * corner.y);
    vec3 world = p.positionSize.xyz + (uCameraRight * rotated.x + uCameraUp * rotated.y) * p.positionSize.w;
    vUv = corner;
    vColor = p.color;
    vParams = p.params.xyz;
    vWorldPos = world;
    gl_Position = uViewProj * vec4(world, 1.0);
}

#pragma stage fragment
in vec2 vUv;
in vec4 vColor;
in vec3 vWorldPos;
flat in vec3 vParams;
out vec4 fragColor;

uniform sampler2D uSceneDepth;
uniform vec2 uSceneSize;
uniform float uNear;
uniform float uFar;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform vec3 uSkyColor;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform mat4 uView;

float linearDepthOf(float d) {
    float z = d * 2.0 - 1.0;
    return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}

void main() {
    float r = length(vUv);
    if (r > 1.0) discard;
    bool glow = vParams.z > 0.5;
    // Billowy edge: radial falloff broken up by two octaves of noise (per-particle seed).
    vec2 q = vUv * 2.3 + vParams.y * 17.0;
    float n = noise(q) * 0.65 + noise(q * 2.1) * 0.35;
    float shape = glow ? pow(1.0 - r, 2.5)
                       : smoothstep(1.0, 0.15, r + (n - 0.5) * 0.9) * (0.35 + 0.65 * smoothstep(0.25, 0.8, n));
    // Soft contact with the scene: fade over the last 0.4 m before the opaque surface.
    vec2 uv = gl_FragCoord.xy / uSceneSize;
    float sceneDepth = linearDepthOf(texture(uSceneDepth, uv).r);
    float myDepth = -(uView * vec4(vWorldPos, 1.0)).z;
    float soft = clamp((sceneDepth - myDepth) / 0.4, 0.0, 1.0);
    float alpha = vColor.a * shape * soft;
    if (glow) {
        fragColor = vec4(vColor.rgb * shape * soft, 0.0); // Additive.
        return;
    }
    if (alpha < 0.003) discard;
    // Lit like a thin volume: sky ambient plus a forward-scattered share of the sun, darker toward
    // the puff's core where the noise is dense.
    vec3 toCamera = normalize(uCameraPos - vWorldPos);
    float forward = pow(max(dot(-toCamera, normalize(uSunDirection)), 0.0), 4.0);
    // No shadow lookup: keep the sun share modest so puffs in the shade do not glow.
    vec3 light = uSkyColor * 0.45 + uSunColor * (0.12 + 0.45 * forward) * (0.6 + 0.4 * n);
    vec3 color = vColor.rgb * light;
    float fog = 1.0 - exp(-length(uCameraPos - vWorldPos) * uFogDensity);
    color = mix(color, mix(uFogColor, uSkyColor, 0.25) * 1.3, fog);
    fragColor = vec4(color * alpha, alpha);
}
