// Shared vertex inputs and transforms. Depth and lit passes must compute gl_Position the same way.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

#ifdef INSTANCED
layout(std430, binding = 0) readonly buffer InstanceBuffer {
    mat4 instanceModels[];
};
uniform int uInstanceOffset;
mat4 modelMatrix() { return instanceModels[uInstanceOffset + gl_InstanceID]; }
#else
uniform mat4 uModel;
mat4 modelMatrix() { return uModel; }
#endif

uniform mat4 uViewProj;
uniform float uTime;
uniform float uWindStrength;
uniform float uWindHeight;

// Wind: slow sway of the whole plant (grows with height) plus fast leaf flutter.
vec3 applyWind(vec3 world, vec3 local, mat4 model) {
#ifdef FOLIAGE
    float h = clamp(local.y / max(uWindHeight, 0.01), 0.0, 1.5);
    vec3 origin = model[3].xyz;
    float phase = dot(origin.xz, vec2(0.13, 0.17));
    float sway = sin(uTime * 1.1 + phase) * 0.6 + sin(uTime * 1.9 + phase * 1.7) * 0.3 + 0.35;
    float flutter = sin(uTime * 6.5 + dot(world, vec3(2.1, 1.7, 2.3))) * 0.05;
    vec3 windDir = normalize(vec3(1.0, 0.0, 0.4));
    world += windDir * sway * uWindStrength * h * h;
    world += vec3(flutter, flutter * 0.5, flutter) * uWindStrength * h;
#endif
    return world;
}
