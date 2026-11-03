// Depth-only pass: shadow cascades and the camera depth prepass.
#pragma stage vertex
layout(location = 0) in vec3 aPosition;

uniform mat4 uModel;
uniform mat4 uViewProj;
// Must match lit.glsl exactly so the lit pass passes the depth test against the prepass.
invariant gl_Position;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    gl_Position = uViewProj * world;
}

#pragma stage fragment
void main() {}
