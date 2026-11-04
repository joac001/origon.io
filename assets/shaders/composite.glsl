// Final composite: bloom, light shafts, exposure, ACES filmic curve, gamma, vignette, dither.
#pragma stage vertex
out vec2 vUv;

void main() {
    vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vUv = positions[gl_VertexID] * 0.5 + 0.5;
    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
}

#pragma stage fragment
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform sampler2D uGodRays;
uniform float uExposure;
uniform float uBloomStrength;
uniform sampler2D uDepth;
uniform sampler2D uBlurred;
uniform float uDof;
uniform float uFocus;
uniform float uNear;
uniform float uFar;

vec3 acesFitted(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec3 hdr = texture(uScene, vUv).rgb;
    if (uDof > 0.001) {
        float z = texture(uDepth, vUv).r * 2.0 - 1.0;
        float linear = 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
        float coc = clamp((linear - uFocus) / max(uFocus * 1.5, 1.0), 0.0, 1.0) * uDof;
        hdr = mix(hdr, texture(uBlurred, vUv).rgb, coc);
    }
    vec3 bloom = texture(uBloom, vUv).rgb;
    vec3 rays = texture(uGodRays, vUv).rgb;
    // Never let a stray NaN/Inf from any pass blank the image.
    if (any(isnan(bloom)) || any(isinf(bloom))) bloom = vec3(0.0);
    if (any(isnan(rays)) || any(isinf(rays))) rays = vec3(0.0);
    hdr = mix(hdr, bloom, uBloomStrength);
    hdr += rays;
    if (any(isnan(hdr)) || any(isinf(hdr))) hdr = vec3(0.0);
    vec3 color = acesFitted(hdr * uExposure);
    color = pow(color, vec3(1.0 / 2.2));

    vec2 centered = vUv - 0.5;
    color *= mix(1.0, 0.8, smoothstep(0.25, 0.75, dot(centered, centered) * 2.0));
    color += (hash(gl_FragCoord.xy) - 0.5) / 255.0;
    fragColor = vec4(color, 1.0);
}
