// Depth-only pass: shadow cascades and the camera depth prepass.
#pragma stage vertex
layout(location = 0) in vec3 aPosition;

uniform mat4 uModel;
uniform mat4 uViewProj;

void main() {
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
}

#pragma stage fragment
void main() {}
