// Water surface: animated normals, screen-space refraction with depth absorption,
// sky reflection with Fresnel, sun glint and shoreline foam.
#pragma stage vertex
layout(location = 0) in vec3 aPosition;

uniform mat4 uModel;
uniform mat4 uViewProj;
out vec3 vWorldPos;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    gl_Position = uViewProj * world;
}

#pragma stage fragment
in vec3 vWorldPos;
out vec4 fragColor;

uniform vec3 uCameraPos;
uniform float uTime;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform vec3 uSkyColor;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform sampler2D uSceneColor;
uniform sampler2D uSceneDepth;
uniform vec2 uSceneSize;
uniform float uNear;
uniform float uFar;
uniform mat4 uView;
uniform mat4 uProj;
uniform int uSsrEnabled;

float linearDepthOf(float d);

// Screen-space reflection: march the reflected ray in view space against the scene depth.
vec4 traceReflection(vec3 worldPos, vec3 reflectDir) {
    vec3 origin = (uView * vec4(worldPos, 1.0)).xyz;
    vec3 dir = normalize((uView * vec4(reflectDir, 0.0)).xyz);
    float stepLength = 0.35;
    vec3 p = origin + dir * 0.2;
    for (int i = 0; i < 40; ++i) {
        p += dir * stepLength;
        stepLength *= 1.09;
        vec4 clip = uProj * vec4(p, 1.0);
        if (clip.w <= 0.0) break;
        vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;
        float sceneDepth = linearDepthOf(texture(uSceneDepth, uv).r);
        float rayDepth = -p.z;
        float diff = rayDepth - sceneDepth;
        if (diff > 0.0 && diff < stepLength * 1.5 + 0.3) {
            vec2 edge = smoothstep(vec2(0.0), vec2(0.12), uv) * smoothstep(vec2(0.0), vec2(0.12), 1.0 - uv);
            float fade = edge.x * edge.y * (1.0 - float(i) / 40.0);
            return vec4(texture(uSceneColor, uv).rgb, fade);
        }
    }
    return vec4(0.0);
}

float linearDepthOf(float d) {
    float z = d * 2.0 - 1.0;
    return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
}

float linearDepth(float d) { return linearDepthOf(d); }

vec2 waveGradient(vec2 p, float t) {
    vec2 g = vec2(0.0);
    // Sum of directional waves (analytic derivatives).
    vec4 dirs[4] = vec4[](vec4(0.8, 0.6, 1.3, 0.035), vec4(-0.4, 0.9, 2.1, 0.02), vec4(0.3, -0.95, 3.7, 0.012),
                          vec4(-0.85, -0.5, 6.3, 0.007));
    for (int i = 0; i < 4; ++i) {
        vec2 d = normalize(dirs[i].xy);
        float k = dirs[i].z;
        float a = dirs[i].w;
        float phase = dot(d, p) * k + t * sqrt(9.8 * k) * 0.6;
        g += d * k * a * cos(phase);
    }
    return g;
}

void main() {
    vec2 gradient = waveGradient(vWorldPos.xz, uTime) + waveGradient(vWorldPos.xz * 2.3 + 11.0, uTime * 1.3) * 0.5;
    vec3 n = normalize(vec3(-gradient.x, 1.0, -gradient.y));
    vec3 v = normalize(uCameraPos - vWorldPos);
    vec2 uv = gl_FragCoord.xy / uSceneSize;

    float sceneDepth = linearDepth(texture(uSceneDepth, uv).r);
    float waterDepth = linearDepth(gl_FragCoord.z);
    float thickness = max(sceneDepth - waterDepth, 0.0);

    // Refraction: offset by the normal, but never sample geometry in front of the water.
    vec2 refractUv = uv + n.xz * 0.04 * clamp(thickness, 0.0, 1.0);
    if (linearDepth(texture(uSceneDepth, refractUv).r) < waterDepth) {
        refractUv = uv;
    }
    vec3 refracted = texture(uSceneColor, refractUv).rgb;
    float refractedThickness = max(linearDepth(texture(uSceneDepth, refractUv).r) - waterDepth, 0.0);
    vec3 absorption = exp(-refractedThickness * vec3(0.45, 0.18, 0.14));
    vec3 deepColor = vec3(0.01, 0.035, 0.035) * (uSkyColor + uSunColor * 0.05);
    vec3 transmitted = refracted * absorption + deepColor * (1.0 - absorption);

    vec3 r = reflect(-v, n);
    vec3 skyReflection = mix(uFogColor * 0.75, uSkyColor * vec3(0.5, 0.58, 0.75), pow(clamp(r.y, 0.0, 1.0), 0.4)) * 0.9;
    float fresnel = 0.03 + 0.7 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
    vec3 reflection = skyReflection;
    if (uSsrEnabled == 1) {
        vec4 ssr = traceReflection(vWorldPos, normalize(reflect(-v, normalize(n * vec3(0.4, 1.0, 0.4)))));
        reflection = mix(skyReflection, ssr.rgb, ssr.a);
    }
    vec3 color = mix(transmitted, reflection, fresnel);

    vec3 h = normalize(v + uSunDirection);
    color += uSunColor * min(pow(max(dot(n, h), 0.0), 800.0) * 1.2, 2.0);

    float foam = (1.0 - smoothstep(0.0, 0.25, thickness)) * (0.5 + 0.5 * sin(uTime * 2.0 + vWorldPos.x * 3.0 + vWorldPos.z * 2.0));
    color = mix(color, vec3(0.5) * (uSkyColor + uSunColor * 0.15), foam * 0.18);

    float dist = length(uCameraPos - vWorldPos);
    float fog = 1.0 - exp(-dist * uFogDensity);
    vec3 fogColor = mix(uFogColor, uSkyColor, 0.25) * 1.3;
    fragColor = vec4(mix(color, fogColor, fog), 1.0);
}
