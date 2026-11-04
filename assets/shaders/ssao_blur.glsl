// 4x4 depth-aware blur that removes the per-pixel SSAO noise pattern.
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

uniform sampler2D uInput;
uniform sampler2D uDepth;
uniform vec2 uTexel;

float linearish(float depth) {
    return 1.0 / max(1.0 - depth, 1e-6);
}

void main() {
    float centerDepth = linearish(texture(uDepth, vUv).r);
    float sum = 0.0;
    float weight = 0.0;
    for (int x = -2; x < 2; ++x) {
        for (int y = -2; y < 2; ++y) {
            vec2 uv = vUv + (vec2(x, y) + 0.5) * uTexel;
            float d = linearish(texture(uDepth, uv).r);
            float w = 1.0 / (1.0 + abs(d - centerDepth) / max(centerDepth, 1e-3) * 40.0);
            sum += texture(uInput, uv).r * w;
            weight += w;
        }
    }
    fragAo = sum / max(weight, 1e-4);
}
