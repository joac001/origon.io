// Forward PBR shading: GGX specular, Lambert diffuse, sun with cascaded PCF soft shadows,
// point lights, hemispheric ambient with SSAO, exponential distance fog, graybox grid.
#pragma stage vertex
#include "include/vertex_common.glsl"

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;
out vec3 vLocalPos;
out vec3 vLocalNormal;
invariant gl_Position;

void main() {
    mat4 model = modelMatrix() * skinMatrix();
    vec4 world = model * vec4(aPosition, 1.0);
    world.xyz = applyWind(world.xyz, aPosition, model);
    vWorldPos = world.xyz;
    vNormal = normalize(transpose(inverse(mat3(model))) * aNormal);
    vUv = aUv;
    vLocalPos = aPosition;
    vLocalNormal = aNormal;
    gl_Position = uViewProj * world;
}

#pragma stage fragment
const int MAX_LIGHTS = 32;
const int MAX_CASCADES = 4;
const float PI = 3.14159265359;

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;
in vec3 vLocalPos;
in vec3 vLocalNormal;
out vec4 fragColor;

uniform int uShading;
uniform sampler2D uAlbedoMap;
uniform sampler2D uNormalMap;
uniform sampler2D uArmMap;
uniform int uHasAlbedoMap;
uniform int uHasNormalMap;
uniform int uHasArmMap;
uniform float uUvScale;
uniform sampler2DArray uTerrainAlbedo;
uniform sampler2DArray uTerrainNormal;
uniform sampler2DArray uTerrainArm;
uniform int uHasTerrainTextures;
uniform float uWaterLevel;

uniform vec3 uCameraPos;
uniform vec3 uAlbedo;
uniform float uRoughness;
uniform float uMetallic;
uniform vec3 uEmissive;
uniform float uGridSize;

uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform vec3 uSkyColor;
uniform vec3 uGroundColor;
uniform float uAmbientIntensity;
uniform vec3 uFogColor;
uniform float uFogDensity;

uniform sampler2DArrayShadow uShadowMap;
uniform mat4 uView;
uniform int uShadowsEnabled;
uniform int uCascadeCount;
uniform mat4 uCascadeViewProj[MAX_CASCADES];
uniform float uCascadeSplit[MAX_CASCADES];
uniform float uCascadeTexel[MAX_CASCADES];
uniform float uCascadeDepthRange[MAX_CASCADES];
uniform sampler2DArray uShadowRaw;
uniform int uSoftShadows;

uniform int uPcfRadius;
uniform float uShadowTexel;

uniform sampler2D uAo;
uniform sampler2D uSceneDepth;
uniform mat4 uProj;
uniform int uContactShadows;

// Short screen-space ray toward the sun: small occluders (grass, pebbles, ledges) that the
// shadow map is too coarse to catch.
float contactShadow() {
    vec3 viewPos = (uView * vec4(vWorldPos, 1.0)).xyz;
    vec3 viewSun = normalize((uView * vec4(uSunDirection, 0.0)).xyz);
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    const int STEPS = 10;
    float maxDistance = 0.6;
    for (int i = 1; i <= STEPS; ++i) {
        vec3 p = viewPos + viewSun * maxDistance * (float(i) - jitter) / float(STEPS);
        vec4 clip = uProj * vec4(p, 1.0);
        vec3 ndc = clip.xyz / clip.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
        float sceneDepth = texture(uSceneDepth, uv).r;
        float rayDepth = ndc.z * 0.5 + 0.5;
        float delta = rayDepth - sceneDepth;
        if (delta > 0.00002 && delta < 0.002) {
            return 0.25;
        }
    }
    return 1.0;
}
uniform int uAoEnabled;
uniform vec2 uSceneSize;

uniform int uLightCount;
uniform vec4 uLightPosRadius[MAX_LIGHTS];
uniform vec3 uLightColor[MAX_LIGHTS];

float distributionGGX(float nDotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geometrySmith(float nDotV, float nDotL, float roughness) {
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float gv = nDotV / (nDotV * (1.0 - k) + k);
    float gl = nDotL / (nDotL * (1.0 - k) + k);
    return gv * gl;
}

vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - cosTheta, 5.0);
}

vec3 brdf(vec3 n, vec3 v, vec3 l, vec3 albedo, float roughness, float metallic) {
    vec3 h = normalize(v + l);
    float nDotL = max(dot(n, l), 0.0);
    float nDotV = max(dot(n, v), 1e-4);
    float nDotH = max(dot(n, h), 0.0);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 f = fresnelSchlick(max(dot(h, v), 0.0), f0);
    float d = distributionGGX(nDotH, roughness);
    float g = geometrySmith(nDotV, nDotL, roughness);
    vec3 specular = d * g * f / max(4.0 * nDotV * nDotL, 1e-4);
    vec3 kd = (1.0 - f) * (1.0 - metallic);
    return (kd * albedo / PI + specular) * nDotL;
}

const vec2 POISSON[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725), vec2(-0.094184101, -0.92938870), vec2(0.34495938, 0.29387760),
    vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464), vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
    vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420), vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
    vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590), vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790));

// Contact-hardening soft shadow: blocker search, penumbra estimate, rotated Poisson PCF.
float pcssShadow(int cascade, vec3 coord) {
    float texel = uShadowTexel;
    float angle = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) * 6.2831853;
    mat2 rotation = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
    float searchRadius = 6.0 * texel;
    float blockerSum = 0.0;
    float blockers = 0.0;
    for (int i = 0; i < 16; ++i) {
        float depth = texture(uShadowRaw, vec3(coord.xy + rotation * POISSON[i] * searchRadius, float(cascade))).r;
        if (depth < coord.z - 0.0005) {
            blockerSum += depth;
            blockers += 1.0;
        }
    }
    if (blockers < 0.5) {
        return 1.0;
    }
    float blocker = blockerSum / blockers;
    // Penumbra in world meters from the depth gap (orthographic, linear depth), then in texels.
    float gapMeters = (coord.z - blocker) * uCascadeDepthRange[cascade];
    float penumbraMeters = gapMeters * 0.035;
    float radius = clamp(penumbraMeters / uCascadeTexel[cascade], 1.0, 14.0) * texel;
    float lit = 0.0;
    for (int i = 0; i < 16; ++i) {
        lit += texture(uShadowMap, vec4(coord.xy + rotation * POISSON[i] * radius, float(cascade), coord.z));
    }
    return lit / 16.0;
}

float sampleCascade(int cascade, vec3 n, float nDotL) {
    // Normal offset scaled by the cascade texel size reduces acne without peter-panning.
    float texel = uCascadeTexel[cascade];
    vec3 offsetPos = vWorldPos + n * texel * (1.5 + 2.0 * (1.0 - nDotL));
    vec4 lightClip = uCascadeViewProj[cascade] * vec4(offsetPos, 1.0);
    vec3 coord = lightClip.xyz / lightClip.w * 0.5 + 0.5;
    if (coord.z > 1.0) {
        return 1.0;
    }
    if (uSoftShadows == 1) {
        return pcssShadow(cascade, coord);
    }
    float sum = 0.0;
    float count = 0.0;
    for (int x = -uPcfRadius; x <= uPcfRadius; ++x) {
        for (int y = -uPcfRadius; y <= uPcfRadius; ++y) {
            vec2 offset = (vec2(x, y) + 0.5 * float(uPcfRadius > 0)) * uShadowTexel;
            sum += texture(uShadowMap, vec4(coord.xy + offset, float(cascade), coord.z));
            count += 1.0;
        }
    }
    return sum / count;
}

float sunShadow(vec3 n) {
    if (uShadowsEnabled == 0 || uCascadeCount == 0) {
        return 1.0;
    }
    float nDotL = max(dot(n, uSunDirection), 0.0);
    float viewDepth = -(uView * vec4(vWorldPos, 1.0)).z;
    int cascade = uCascadeCount;
    for (int i = 0; i < uCascadeCount; ++i) {
        if (viewDepth < uCascadeSplit[i]) {
            cascade = i;
            break;
        }
    }
    if (cascade >= uCascadeCount) {
        return 1.0;
    }
    float shadow = sampleCascade(cascade, n, nDotL);
    // Blend into the next cascade near the split to hide the seam.
    float splitStart = cascade == 0 ? 0.0 : uCascadeSplit[cascade - 1];
    float blend = smoothstep(0.85, 1.0, (viewDepth - splitStart) / (uCascadeSplit[cascade] - splitStart));
    if (blend > 0.0) {
        float next = cascade + 1 < uCascadeCount ? sampleCascade(cascade + 1, n, nDotL) : 1.0;
        shadow = mix(shadow, next, blend);
    }
    return shadow;
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), u.x), mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), u.x), u.y);
}

float fbm2(vec2 p) {
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 4; ++i) {
        sum += noise2(p) * amp;
        p = p * 2.07 + vec2(17.1, 3.7);
        amp *= 0.5;
    }
    return sum;
}

// Forest ground: grass, needle litter, moss, dirt paths and rock on steep slopes.
void terrainMaterial(inout vec3 n, out vec3 albedo, out float roughness) {
    vec2 p = vWorldPos.xz;
    float slope = 1.0 - n.y;
    float large = fbm2(p * 0.045);
    float medium = fbm2(p * 0.35);
    float fine = noise2(p * 3.1);

    vec3 grass = mix(vec3(0.07, 0.11, 0.03), vec3(0.13, 0.17, 0.05), medium) * (0.85 + 0.3 * fine);
    vec3 litter = mix(vec3(0.11, 0.07, 0.04), vec3(0.17, 0.11, 0.06), medium) * (0.8 + 0.4 * fine);
    vec3 moss = vec3(0.06, 0.11, 0.02) * (0.8 + 0.4 * fine);
    vec3 dirt = mix(vec3(0.14, 0.1, 0.07), vec3(0.2, 0.15, 0.1), fine);
    vec3 rock = mix(vec3(0.16, 0.16, 0.15), vec3(0.28, 0.27, 0.25), fbm2(p * 1.3 + vWorldPos.y * 0.7));

    albedo = mix(grass, litter, smoothstep(0.42, 0.62, large + medium * 0.25));
    albedo = mix(albedo, moss, smoothstep(0.55, 0.75, fbm2(p * 0.12 + 9.0)) * 0.7);
    albedo = mix(albedo, dirt, clamp(vUv.x * 1.2, 0.0, 1.0));
    float rockMask = smoothstep(0.32, 0.5, slope + (medium - 0.5) * 0.2);
    // Moss creeps onto the tops of rocks.
    vec3 mossyRock = mix(rock, moss, smoothstep(0.55, 0.8, n.y + fine * 0.2) * 0.8);
    albedo = mix(albedo, mossyRock, rockMask);
    roughness = mix(0.95, 0.8, rockMask);

    // Micro relief from the noise gradient.
    float e = 0.15;
    float hx = fbm2((p + vec2(e, 0.0)) * 1.7) - fbm2((p - vec2(e, 0.0)) * 1.7);
    float hz = fbm2((p + vec2(0.0, e)) * 1.7) - fbm2((p - vec2(0.0, e)) * 1.7);
    n = normalize(n + vec3(-hx, 0.0, -hz) * mix(0.6, 1.4, rockMask));
}

// Normal mapping without precomputed tangents (cotangent frame from screen derivatives).
mat3 cotangentFrame(vec3 n, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, n);
    vec3 dp1perp = cross(n, dp1);
    vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(max(dot(t, t), dot(b, b)), 1e-12));
    return mat3(t * invmax, b * invmax, n);
}

// Textured forest ground from CC0 scans. Layers: 0 grass, 1 needles, 2 leaves, 3 path, 4 rock, 5 creek bed.
const float TERRAIN_TILE = 3.0;

float g_farMix = 0.0; // Set per fragment: 0 near, 1 far (anti-tiling blend).

void sampleLayer(int layer, vec2 uv, float weight, inout vec3 albedo, inout vec3 tn, inout vec2 rough_ao) {
    if (weight <= 0.001) return;
    vec3 near = texture(uTerrainAlbedo, vec3(uv, layer)).rgb;
    if (g_farMix > 0.001) {
        // Same texture at a lower, rotated frequency hides the repeating pattern at a distance.
        vec2 farUv = mat2(0.8, -0.6, 0.6, 0.8) * uv * 0.27;
        near = mix(near, texture(uTerrainAlbedo, vec3(farUv, layer)).rgb, g_farMix * 0.6);
    }
    albedo += near * weight;
    tn += (texture(uTerrainNormal, vec3(uv, layer)).xyz * 2.0 - 1.0) * weight;
    vec3 arm = texture(uTerrainArm, vec3(uv, layer)).rgb;
    rough_ao += vec2(arm.g, arm.r) * weight;
}

void terrainTextured(inout vec3 n, out vec3 albedo, out float roughness, out float ao) {
    vec3 geomN = n;
    vec2 p = vWorldPos.xz;
    float slope = 1.0 - geomN.y;
    float large = fbm2(p * 0.035);
    float medium = fbm2(p * 0.22);
    float breakup = (noise2(p * 1.7) - 0.5) * 0.25;

    float wGrass = smoothstep(0.5, 0.7, large + breakup);
    float wLeaves = smoothstep(0.55, 0.72, fbm2(p * 0.06 + 13.0) + breakup) * (1.0 - wGrass * 0.7);
    float wNeedles = max(1.0 - wGrass - wLeaves, 0.0);
    float wPath = smoothstep(0.25, 0.6, vUv.x + breakup);
    float wCreek = 1.0 - smoothstep(uWaterLevel + 0.25, uWaterLevel + 0.9, vWorldPos.y + breakup);
    float wRock = smoothstep(0.3, 0.45, slope + (medium - 0.5) * 0.15);

    // Planar layers projected from above.
    g_farMix = smoothstep(12.0, 55.0, length(uCameraPos - vWorldPos));
    vec2 uv = p / TERRAIN_TILE;
    float other = (1.0 - wPath) * (1.0 - wCreek);
    vec3 planarAlbedo = vec3(0.0);
    vec3 tn = vec3(0.0);
    vec2 roughAo = vec2(0.0);
    vec3 before = planarAlbedo;
    sampleLayer(0, uv, wGrass * other, planarAlbedo, tn, roughAo);
    planarAlbedo = before + (planarAlbedo - before) * vec3(0.6, 0.75, 0.45); // Greener, darker grass.
    sampleLayer(1, uv * 1.3, wNeedles * other, planarAlbedo, tn, roughAo);
    sampleLayer(2, uv, wLeaves * other, planarAlbedo, tn, roughAo);
    sampleLayer(3, uv * 0.8, wPath * (1.0 - wCreek), planarAlbedo, tn, roughAo);
    sampleLayer(5, uv * 0.7, wCreek, planarAlbedo, tn, roughAo);
    vec3 planarN = normalize(vec3(tn.x + geomN.x, abs(tn.z) * geomN.y, tn.y + geomN.z));

    // Rock: triplanar so steep banks do not stretch.
    vec3 blend = pow(abs(geomN), vec3(4.0));
    blend /= (blend.x + blend.y + blend.z);
    vec3 q = vWorldPos / 4.0;
    vec3 rockAlbedo = texture(uTerrainAlbedo, vec3(q.zy, 4)).rgb * blend.x + texture(uTerrainAlbedo, vec3(q.xz, 4)).rgb * blend.y +
                      texture(uTerrainAlbedo, vec3(q.xy, 4)).rgb * blend.z;
    vec3 tnX = texture(uTerrainNormal, vec3(q.zy, 4)).xyz * 2.0 - 1.0;
    vec3 tnY = texture(uTerrainNormal, vec3(q.xz, 4)).xyz * 2.0 - 1.0;
    vec3 tnZ = texture(uTerrainNormal, vec3(q.xy, 4)).xyz * 2.0 - 1.0;
    vec3 nx = vec3(abs(tnX.z) * geomN.x, tnX.y + geomN.y, tnX.x + geomN.z);
    vec3 ny = vec3(tnY.x + geomN.x, abs(tnY.z) * geomN.y, tnY.y + geomN.z);
    vec3 nz = vec3(tnZ.x + geomN.x, tnZ.y + geomN.y, abs(tnZ.z) * geomN.z);
    vec3 rockN = normalize(nx * blend.x + ny * blend.y + nz * blend.z);
    vec3 rockArm = texture(uTerrainArm, vec3(q.xz, 4)).rgb;

    // Macro variation hides tiling.
    float macro = (0.7 + 0.3 * fbm2(p * 0.02 + 5.0)) * 0.85;
    albedo = mix(planarAlbedo, rockAlbedo, wRock) * macro;
    albedo = mix(vec3(dot(albedo, vec3(0.3, 0.59, 0.11))), albedo, 0.8); // Slightly muted, like the reference.
    n = normalize(mix(planarN, rockN, wRock));
    roughness = mix(roughAo.x, rockArm.g, wRock);
    ao = mix(roughAo.y, rockArm.r, wRock);
    // Damp ground near the water is darker and glossier.
    float wet = 1.0 - smoothstep(uWaterLevel, uWaterLevel + 0.5, vWorldPos.y);
    albedo *= mix(1.0, 0.55, wet);
    roughness = mix(roughness, 0.25, wet);
}

float gridLines(vec3 worldPos, vec3 n, float size) {
    vec3 weights = abs(n);
    weights /= (weights.x + weights.y + weights.z);
    vec3 cell = worldPos / size;
    vec3 width = fwidth(cell) * 1.2;
    vec3 line = 1.0 - smoothstep(vec3(0.0), width, abs(fract(cell - 0.5) - 0.5));
    // Each projection uses the two axes that lie in the plane.
    float lx = max(line.y, line.z);
    float ly = max(line.x, line.z);
    float lz = max(line.x, line.y);
    return lx * weights.x + ly * weights.y + lz * weights.z;
}

void main() {
    vec3 n = normalize(vNormal);
    if (!gl_FrontFacing) {
        n = -n;
    }
    vec3 v = normalize(uCameraPos - vWorldPos);
    vec3 albedo = uAlbedo;
    float roughness = clamp(uRoughness, 0.05, 1.0);
    float metallic = uMetallic;
    float occlusion = 1.0;
    float alpha = 1.0;
    vec2 uv = vUv * uUvScale;
    if (uShading == 1) {
        if (uHasTerrainTextures == 1) {
            terrainTextured(n, albedo, roughness, occlusion);
        } else {
            terrainMaterial(n, albedo, roughness);
        }
    }
    if (uShading == 4) {
        // Object-space triplanar (weapons): texture follows the model, 15 cm tiles.
        vec3 ln = normalize(vLocalNormal);
        vec3 blend = pow(abs(ln), vec3(4.0));
        blend /= (blend.x + blend.y + blend.z);
        vec3 q = vLocalPos / 0.15;
        albedo *= texture(uAlbedoMap, q.zy).rgb * blend.x + texture(uAlbedoMap, q.xz).rgb * blend.y + texture(uAlbedoMap, q.xy).rgb * blend.z;
        vec3 arm = texture(uArmMap, q.zy).rgb * blend.x + texture(uArmMap, q.xz).rgb * blend.y + texture(uArmMap, q.xy).rgb * blend.z;
        roughness = clamp(arm.g * 0.9, 0.08, 1.0);
        occlusion = arm.r;
    } else if (uShading == 3) {
        // Triplanar textured block (map geometry): world-space mapping, 1.5 m tiles.
        vec3 blend = pow(abs(n), vec3(4.0));
        blend /= (blend.x + blend.y + blend.z);
        vec3 q = vWorldPos / 1.5;
        albedo *= texture(uAlbedoMap, q.zy).rgb * blend.x + texture(uAlbedoMap, q.xz).rgb * blend.y + texture(uAlbedoMap, q.xy).rgb * blend.z;
        vec3 arm = texture(uArmMap, q.zy).rgb * blend.x + texture(uArmMap, q.xz).rgb * blend.y + texture(uArmMap, q.xy).rgb * blend.z;
        occlusion = arm.r;
        roughness = clamp(arm.g, 0.05, 1.0);
        vec3 tnX = texture(uNormalMap, q.zy).xyz * 2.0 - 1.0;
        vec3 tnY = texture(uNormalMap, q.xz).xyz * 2.0 - 1.0;
        vec3 tnZ = texture(uNormalMap, q.xy).xyz * 2.0 - 1.0;
        vec3 nx = vec3(abs(tnX.z) * n.x, tnX.y + n.y, tnX.x + n.z);
        vec3 ny = vec3(tnY.x + n.x, abs(tnY.z) * n.y, tnY.y + n.z);
        vec3 nz = vec3(tnZ.x + n.x, tnZ.y + n.y, abs(tnZ.z) * n.z);
        n = normalize(nx * blend.x + ny * blend.y + nz * blend.z);
    } else if (uHasAlbedoMap == 1) {
        vec4 tex = texture(uAlbedoMap, uv);
        albedo *= tex.rgb;
        alpha = tex.a;
    }
#if defined(FOLIAGE) || defined(ALPHA_TEST)
    // Sharpened alpha for alpha-to-coverage; plain alpha test otherwise.
    alpha = (alpha - 0.5) / max(fwidth(alpha), 1e-4) + 0.5;
    if (alpha < 0.02) {
        discard;
    }
#endif
    if (uHasNormalMap == 1 && uShading < 3) {
        vec3 mapped = texture(uNormalMap, uv).xyz * 2.0 - 1.0;
        n = normalize(cotangentFrame(n, vWorldPos, uv) * mapped);
    }
    if (uHasArmMap == 1 && uShading < 3) {
        vec3 arm = texture(uArmMap, uv).rgb;
        occlusion = arm.r;
        roughness = clamp(arm.g * uRoughness / 0.8, 0.05, 1.0);
        metallic = arm.b;
    }

    if (uGridSize > 0.0) {
        float minor = gridLines(vWorldPos, n, uGridSize);
        float major = gridLines(vWorldPos, n, uGridSize * 4.0);
        albedo *= 1.0 - 0.18 * minor - 0.22 * major;
    }

    float shadow = sunShadow(n);
    if (uContactShadows == 1 && shadow > 0.05 && uShading != 2) {
        shadow *= contactShadow();
    }
    vec3 color = brdf(n, v, uSunDirection, albedo, roughness, metallic) * uSunColor * shadow;

    for (int i = 0; i < uLightCount; ++i) {
        vec3 toLight = uLightPosRadius[i].xyz - vWorldPos;
        float dist = length(toLight);
        float radius = uLightPosRadius[i].w;
        if (dist >= radius) {
            continue;
        }
        float falloff = pow(clamp(1.0 - pow(dist / radius, 4.0), 0.0, 1.0), 2.0) / (dist * dist + 1.0);
        color += brdf(n, v, toLight / dist, albedo, roughness, metallic) * uLightColor[i] * falloff;
    }

    float ao = uAoEnabled == 1 ? texture(uAo, gl_FragCoord.xy / uSceneSize).r : 1.0;
    vec3 ambient = mix(uGroundColor, uSkyColor, n.y * 0.5 + 0.5) * uAmbientIntensity;
    // Cheap ambient specular: sky reflection tinted by roughness.
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 reflected = reflect(-v, n);
    vec3 envSpecular = mix(uGroundColor, uSkyColor, reflected.y * 0.5 + 0.5) * uAmbientIntensity *
                       fresnelSchlick(max(dot(n, v), 0.0), f0) * (1.0 - roughness * 0.8);
#ifdef FOLIAGE
    envSpecular *= 0.15;
#endif
    color += (ambient * albedo * (1.0 - metallic) + envSpecular) * ao * occlusion;
#ifdef FOLIAGE
    // Light transmitted through leaves when looking toward the sun, plus soft wrap lighting.
    float shadowTerm = sunShadow(normalize(vNormal));
    float backLit = pow(max(dot(-v, uSunDirection), 0.0), 3.0);
    color += albedo * uSunColor * (backLit * 0.5 + 0.12) * shadowTerm;
#endif
    color += uEmissive;

    float dist = length(uCameraPos - vWorldPos);
    float fog = 1.0 - exp(-dist * uFogDensity);
    float sunAmount = max(dot(-v, uSunDirection), 0.0);
    vec3 fogColor = mix(uFogColor, uSkyColor, 0.25) * 1.3 + uSunColor * pow(sunAmount, 8.0) * 0.025;
    color = mix(color, fogColor, fog);

    fragColor = vec4(color, clamp(alpha, 0.0, 1.0));
}
