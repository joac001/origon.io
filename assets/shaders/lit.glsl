// Forward PBR shading: GGX specular, Lambert diffuse, sun with cascaded PCF soft shadows,
// point lights, hemispheric ambient with SSAO, exponential distance fog, graybox grid.
#pragma stage vertex
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vNormal = normalize(transpose(inverse(mat3(uModel))) * aNormal);
    gl_Position = uViewProj * world;
}

#pragma stage fragment
const int MAX_LIGHTS = 32;
const int MAX_CASCADES = 4;
const float PI = 3.14159265359;

in vec3 vWorldPos;
in vec3 vNormal;
out vec4 fragColor;

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

    if (uGridSize > 0.0) {
        float minor = gridLines(vWorldPos, n, uGridSize);
        float major = gridLines(vWorldPos, n, uGridSize * 4.0);
        albedo *= 1.0 - 0.18 * minor - 0.22 * major;
    }

    vec3 color = brdf(n, v, uSunDirection, albedo, roughness, uMetallic) * uSunColor * sunShadow(n);

    for (int i = 0; i < uLightCount; ++i) {
        vec3 toLight = uLightPosRadius[i].xyz - vWorldPos;
        float dist = length(toLight);
        float radius = uLightPosRadius[i].w;
        if (dist >= radius) {
            continue;
        }
        float falloff = pow(clamp(1.0 - pow(dist / radius, 4.0), 0.0, 1.0), 2.0) / (dist * dist + 1.0);
        color += brdf(n, v, toLight / dist, albedo, roughness, uMetallic) * uLightColor[i] * falloff;
    }

    float ao = uAoEnabled == 1 ? texture(uAo, gl_FragCoord.xy / uSceneSize).r : 1.0;
    vec3 ambient = mix(uGroundColor, uSkyColor, n.y * 0.5 + 0.5) * uAmbientIntensity;
    // Cheap ambient specular: sky reflection tinted by roughness.
    vec3 f0 = mix(vec3(0.04), albedo, uMetallic);
    vec3 reflected = reflect(-v, n);
    vec3 envSpecular = mix(uGroundColor, uSkyColor, reflected.y * 0.5 + 0.5) * uAmbientIntensity *
                       fresnelSchlick(max(dot(n, v), 0.0), f0) * (1.0 - roughness * 0.8);
    color += (ambient * albedo * (1.0 - uMetallic) + envSpecular) * ao;
    color += uEmissive;

    float dist = length(uCameraPos - vWorldPos);
    float fog = 1.0 - exp(-dist * uFogDensity);
    float sunAmount = max(dot(-v, uSunDirection), 0.0);
    vec3 fogColor = uFogColor * 1.6 + uSunColor * pow(sunAmount, 8.0) * 0.05;
    color = mix(color, fogColor, fog);

    fragColor = vec4(color, 1.0);
}
