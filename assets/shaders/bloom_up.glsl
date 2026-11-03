// Bloom upsample: 3x3 tent filter, added onto the next larger mip.
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
uniform vec2 uSourceTexel;
uniform float uRadius;

void main() {
    vec2 o = uSourceTexel * uRadius;
    vec3 color = texture(uSource, vUv).rgb * 4.0;
    color += (texture(uSource, vUv + vec2(-o.x, 0)).rgb + texture(uSource, vUv + vec2(o.x, 0)).rgb +
              texture(uSource, vUv + vec2(0, -o.y)).rgb + texture(uSource, vUv + vec2(0, o.y)).rgb) * 2.0;
    color += texture(uSource, vUv + vec2(-o.x, -o.y)).rgb + texture(uSource, vUv + vec2(o.x, -o.y)).rgb +
             texture(uSource, vUv + vec2(-o.x, o.y)).rgb + texture(uSource, vUv + vec2(o.x, o.y)).rgb;
    fragColor = color / 16.0;
}
