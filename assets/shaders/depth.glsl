// Depth-only pass: shadow cascades and the camera depth prepass.
#pragma stage vertex
#include "include/vertex_common.glsl"
out vec2 vUv;
// Must match lit.glsl exactly so the lit pass passes the depth test against the prepass.
invariant gl_Position;

void main() {
    mat4 model = modelMatrix() * skinMatrix();
    vec4 world = model * vec4(aPosition, 1.0);
    world.xyz = applyWind(world.xyz, aPosition, model);
    vUv = aUv;
    gl_Position = uViewProj * world;
}

#pragma stage fragment
in vec2 vUv;
#if defined(FOLIAGE) || defined(ALPHA_TEST)
uniform sampler2D uAlbedoMap;
#endif

void main() {
#if defined(FOLIAGE) || defined(ALPHA_TEST)
    if (texture(uAlbedoMap, vUv).a < 0.5) {
        discard;
    }
#endif
}
