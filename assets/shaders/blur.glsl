// 9-tap separable Gaussian blur.
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

uniform sampler2D uSource;
uniform vec2 uDirection;

void main() {
    const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 color = texture(uSource, vUv).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        vec2 offset = uDirection * float(i) * 1.6;
        color += texture(uSource, vUv + offset).rgb * weights[i];
        color += texture(uSource, vUv - offset).rgb * weights[i];
    }
    fragColor = color;
}
