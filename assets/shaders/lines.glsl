// Colored debug and editor lines.
#pragma stage vertex
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

uniform mat4 uViewProj;
out vec4 vColor;

void main() {
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}

#pragma stage fragment
in vec4 vColor;
out vec4 fragColor;

void main() {
    fragColor = vColor;
}
