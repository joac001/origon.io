// Screen-space light shafts: radial blur of visible sky near the sun, occluded by geometry.
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

uniform sampler2D uDepth;
uniform vec2 uSunUv;
uniform float uAspect;
uniform vec3 uSunColor;
uniform float uIntensity;

const int STEPS = 48;

float sunMask(vec2 uv) {
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        return 0.0;
    }
    float sky = texture(uDepth, uv).r >= 1.0 ? 1.0 : 0.0;
    vec2 d = (uv - uSunUv) * vec2(uAspect, 1.0);
    return sky * exp(-dot(d, d) * 6.0);
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 delta = (uSunUv - vUv) / float(STEPS) * 0.9;
    vec2 uv = vUv + delta * hash(gl_FragCoord.xy);
    float decay = 1.0;
    float light = 0.0;
    for (int i = 0; i < STEPS; ++i) {
        light += sunMask(uv) * decay;
        decay *= 0.965;
        uv += delta;
    }
    fragColor = uSunColor * light / float(STEPS) * uIntensity * 4.0;
}
