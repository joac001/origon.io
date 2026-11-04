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

#ifdef SKINNED
layout(location = 3) in uvec4 aJoints;
layout(location = 4) in vec4 aWeights;
// All skin palettes of the frame live in one buffer; uBoneOffset selects this character's.
layout(std430, binding = 1) readonly buffer BonePalettes {
    mat4 uBones[];
};
uniform int uBoneOffset;
// Bitmask of joints to draw (0 = all): first-person arms keep only arm-weighted geometry.
uniform uint uJointMask;
float jointVisibility() {
    if (uJointMask == 0u) return 1.0;
    float visible = 0.0;
    for (int i = 0; i < 4; ++i) {
        uint joint = aJoints[i];
        if (joint < 32u && (uJointMask & (1u << joint)) != 0u) visible += aWeights[i];
    }
    return visible;
}
mat4 skinMatrix() {
    return uBones[uBoneOffset + int(aJoints.x)] * aWeights.x + uBones[uBoneOffset + int(aJoints.y)] * aWeights.y +
           uBones[uBoneOffset + int(aJoints.z)] * aWeights.z + uBones[uBoneOffset + int(aJoints.w)] * aWeights.w;
}
#else
mat4 skinMatrix() { return mat4(1.0); }
float jointVisibility() { return 1.0; }
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
