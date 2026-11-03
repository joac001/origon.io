// Forward PBR shading: GGX specular, Lambert diffuse, sun with cascaded PCF soft shadows,
// point lights, hemispheric ambient with SSAO, exponential distance fog, graybox grid.
#pragma stage vertex
#include "include/vertex_common.glsl"

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;
invariant gl_Position;

void main() {
    mat4 model = modelMatrix();
    vec4 world = model * vec4(aPosition, 1.0);
    world.xyz = applyWind(world.xyz, aPosition, model);
    vWorldPos = world.xyz;
    vNormal = normalize(transpose(inverse(mat3(model))) * aNormal);
    vUv = aUv;
    gl_Position = uViewProj * world;
}

#pragma stage fragment
const int MAX_LIGHTS = 32;
const int MAX_CASCADES = 4;
const float PI = 3.14159265359;

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;
out vec4 fragColor;

uniform int uShading;
uniform sampler2D uAlbedoMap;
uniform sampler2D uNormalMap;
uniform sampler2D uArmMap;
uniform int uHasAlbedoMap;
uniform int uHasNormalMap;
uniform int uHasArmMap;
uniform float uUvScale;

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
uniform int uPcfRadius;
uniform float uShadowTexel;

uniform sampler2D uAo;
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

float sampleCascade(int cascade, vec3 n, float nDotL) {
    // Normal offset scaled by the cascade texel size reduces acne without peter-panning.
    float texel = uCascadeTexel[cascade];
    vec3 offsetPos = vWorldPos + n * texel * (1.5 + 2.0 * (1.0 - nDotL));
    vec4 lightClip = uCascadeViewProj[cascade] * vec4(offsetPos, 1.0);
    vec3 coord = lightClip.xyz / lightClip.w * 0.5 + 0.5;
    if (coord.z > 1.0) {
        return 1.0;
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
        terrainMaterial(n, albedo, roughness);
    }
    if (uHasAlbedoMap == 1) {
        vec4 tex = texture(uAlbedoMap, uv);
        albedo *= tex.rgb;
        alpha = tex.a;
    }
#ifdef FOLIAGE
    // Sharpened alpha for alpha-to-coverage; plain alpha test otherwise.
    alpha = (alpha - 0.5) / max(fwidth(alpha), 1e-4) + 0.5;
    if (alpha < 0.02) {
        discard;
    }
#endif
    if (uHasNormalMap == 1) {
        vec3 mapped = texture(uNormalMap, uv).xyz * 2.0 - 1.0;
        n = normalize(cotangentFrame(n, vWorldPos, uv) * mapped);
    }
    if (uHasArmMap == 1) {
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

    vec3 color = brdf(n, v, uSunDirection, albedo, roughness, metallic) * uSunColor * sunShadow(n);

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
    vec3 fogColor = mix(uFogColor, uSkyColor, 0.25) * 1.3 + uSunColor * pow(sunAmount, 8.0) * 0.06;
    color = mix(color, fogColor, fog);

    fragColor = vec4(color, clamp(alpha, 0.0, 1.0));
}
