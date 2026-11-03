// Procedural sky: vertical gradient, horizon haze and sun disk.
#pragma stage vertex
out vec2 vNdc;

void main() {
    vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vNdc = positions[gl_VertexID];
    gl_Position = vec4(vNdc, 1.0, 1.0); // Far plane: only where no geometry was drawn.
}

#pragma stage fragment
in vec2 vNdc;
out vec4 fragColor;

uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform vec3 uSkyColor;
uniform vec3 uFogColor;

void main() {
    vec4 farPoint = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(farPoint.xyz / farPoint.w - uCameraPos);

    float height = clamp(dir.y, -1.0, 1.0);
    vec3 zenith = uSkyColor * 0.75;
    vec3 sky = mix(uFogColor, zenith, pow(clamp(height, 0.0, 1.0), 0.45));
    sky = mix(sky, uFogColor * 0.6, clamp(-height * 4.0, 0.0, 1.0));

    float sunDot = max(dot(dir, uSunDirection), 0.0);
    sky += uSunColor * (pow(sunDot, 2000.0) * 8.0 + pow(sunDot, 12.0) * 0.08);

    fragColor = vec4(sky * 1.6, 1.0);
}
