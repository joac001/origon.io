#include "render/Renderer.h"

#include "core/Log.h"

#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace origon {
namespace {

struct ShadowConfig {
    int size;
    int cascades;
    int pcfRadius;
    float distance;
};

ShadowConfig shadowConfig(int quality) {
    switch (quality) {
    case 1: return {1024, 2, 0, 60.0f};
    case 2: return {2048, 3, 1, 100.0f};
    case 3: return {2048, 4, 1, 150.0f};
    case 4: return {4096, 4, 2, 200.0f};
    default: return {0, 0, 0, 0.0f};
    }
}

unsigned makeTexture2D(GLenum format, glm::ivec2 size, GLenum filter) {
    unsigned texture = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &texture);
    glTextureStorage2D(texture, 1, format, std::max(size.x, 1), std::max(size.y, 1));
    glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, filter);
    glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, filter);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

unsigned makeColorFbo(unsigned texture) {
    unsigned fbo = 0;
    glCreateFramebuffers(1, &fbo);
    glNamedFramebufferTexture(fbo, GL_COLOR_ATTACHMENT0, texture, 0);
    return fbo;
}

void deleteTexture(unsigned& texture) {
    if (texture) glDeleteTextures(1, &texture);
    texture = 0;
}

void deleteFbo(unsigned& fbo) {
    if (fbo) glDeleteFramebuffers(1, &fbo);
    fbo = 0;
}

unsigned makeSolidTexture(float value) {
    unsigned texture = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &texture);
    glTextureStorage2D(texture, 1, GL_RGBA8, 1, 1);
    const unsigned char pixel[4] = {static_cast<unsigned char>(value * 255.0f), static_cast<unsigned char>(value * 255.0f),
                                    static_cast<unsigned char>(value * 255.0f), 255};
    glTextureSubImage2D(texture, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    return texture;
}

void drawFullscreen(unsigned vao) {
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

} // namespace

bool Renderer::init() {
    bool ok = true;
    ok &= m_litShader.loadFile("assets/shaders/lit.glsl");
    ok &= m_litInstanced.loadFile("assets/shaders/lit.glsl", "#define INSTANCED\n");
    ok &= m_litFoliage.loadFile("assets/shaders/lit.glsl", "#define INSTANCED\n#define FOLIAGE\n");
    ok &= m_depthShader.loadFile("assets/shaders/depth.glsl");
    ok &= m_depthInstanced.loadFile("assets/shaders/depth.glsl", "#define INSTANCED\n");
    ok &= m_depthFoliage.loadFile("assets/shaders/depth.glsl", "#define INSTANCED\n#define FOLIAGE\n");
    ok &= m_skyShader.loadFile("assets/shaders/sky.glsl");
    ok &= m_compositeShader.loadFile("assets/shaders/composite.glsl");
    ok &= m_ssaoShader.loadFile("assets/shaders/ssao.glsl");
    ok &= m_ssaoBlurShader.loadFile("assets/shaders/ssao_blur.glsl");
    ok &= m_bloomDownShader.loadFile("assets/shaders/bloom_down.glsl");
    ok &= m_bloomUpShader.loadFile("assets/shaders/bloom_up.glsl");
    ok &= m_godRaysShader.loadFile("assets/shaders/godrays.glsl");
    ok &= m_lineShader.loadFile("assets/shaders/lines.glsl");
    ok &= m_waterShader.loadFile("assets/shaders/water.glsl");
    ok &= m_blurShader.loadFile("assets/shaders/blur.glsl");
    ok &= m_volumetricShader.loadFile("assets/shaders/volumetric.glsl");
    if (!ok) {
        log::error("Renderer: shader loading failed");
        return false;
    }
    glCreateVertexArrays(1, &m_emptyVao);

    glCreateVertexArrays(1, &m_lineVao);
    glEnableVertexArrayAttrib(m_lineVao, 0);
    glVertexArrayAttribFormat(m_lineVao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(LineVertex, position));
    glVertexArrayAttribBinding(m_lineVao, 0, 0);
    glEnableVertexArrayAttrib(m_lineVao, 1);
    glVertexArrayAttribFormat(m_lineVao, 1, 4, GL_FLOAT, GL_FALSE, offsetof(LineVertex, color));
    glVertexArrayAttribBinding(m_lineVao, 1, 0);

    glCreateSamplers(1, &m_shadowRawSampler);
    glSamplerParameteri(m_shadowRawSampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(m_shadowRawSampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(m_shadowRawSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(m_shadowRawSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(m_shadowRawSampler, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    m_whiteTexture = makeSolidTexture(1.0f);
    m_blackTexture = makeSolidTexture(0.0f);
    m_cube = Mesh::makeCube();
    m_capsule = Mesh::makeCapsule(0.4f, 1.0f);
    m_sphere = Mesh::makeCapsule(0.5f, 0.0f, 16, 8);
    return true;
}

void Renderer::beginFrame(const RenderSettings& settings, glm::ivec2 framebufferSize) {
    m_settings = settings;
    m_settings.renderScale = std::clamp(m_settings.renderScale, 0.25f, 2.0f);
    m_outputSize = glm::max(framebufferSize, glm::ivec2(1));
    m_items.clear();
    m_viewModelItems.clear();
    m_transparentItems.clear();
    m_lights.clear();
    m_lines.clear();
    m_overlayLines.clear();
    m_stats = {};
    ensureTargets();
    ensureShadowMaps();
}

void Renderer::addLight(const PointLight& light) {
    if (static_cast<int>(m_lights.size()) < kMaxPointLights) {
        m_lights.push_back(light);
    }
}

void Renderer::addLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color, bool overlay) {
    auto& target = overlay ? m_overlayLines : m_lines;
    target.push_back({a, color});
    target.push_back({b, color});
}

glm::mat4 Renderer::projection(const CameraView& camera) const {
    const float aspect = static_cast<float>(m_outputSize.x) / static_cast<float>(std::max(m_outputSize.y, 1));
    return glm::perspective(glm::radians(camera.fovYDegrees), aspect, camera.nearPlane, camera.farPlane);
}

// --- Targets -----------------------------------------------------------------------------

void Renderer::destroyTargets() {
    deleteFbo(m_msaaFbo);
    if (m_msaaColor) glDeleteRenderbuffers(1, &m_msaaColor);
    if (m_msaaDepth) glDeleteRenderbuffers(1, &m_msaaDepth);
    m_msaaColor = m_msaaDepth = 0;
    deleteFbo(m_resolveFbo);
    deleteTexture(m_resolveColor);
    deleteTexture(m_resolveDepth);
    deleteFbo(m_ssaoFbo);
    deleteTexture(m_ssaoRaw);
    deleteFbo(m_ssaoBlurFbo);
    deleteTexture(m_ssaoBlurred);
    for (int i = 0; i < kBloomMips; ++i) {
        deleteFbo(m_bloomFbos[static_cast<size_t>(i)]);
        deleteTexture(m_bloomTextures[static_cast<size_t>(i)]);
    }
    deleteFbo(m_godRaysFbo);
    deleteTexture(m_godRaysTexture);
    for (int i = 0; i < 2; ++i) {
        deleteFbo(m_dofFbo[i]);
        deleteTexture(m_dofTexture[i]);
    }
    deleteFbo(m_copyFbo);
    deleteTexture(m_copyColor);
    deleteTexture(m_copyDepth);
}

void Renderer::ensureTargets() {
    const glm::ivec2 sceneSize = glm::max(glm::ivec2(glm::vec2(m_outputSize) * m_settings.renderScale), glm::ivec2(1));
    const int samples = m_settings.msaaSamples >= 2 ? std::min(m_settings.msaaSamples, 8) : 0;
    if (sceneSize == m_sceneSize && samples == m_targetSamples && m_resolveFbo) {
        return;
    }
    destroyTargets();
    m_sceneSize = sceneSize;
    m_targetSamples = samples;

    m_resolveColor = makeTexture2D(GL_RGBA16F, sceneSize, GL_LINEAR);
    m_resolveDepth = makeTexture2D(GL_DEPTH_COMPONENT32F, sceneSize, GL_NEAREST);
    glCreateFramebuffers(1, &m_resolveFbo);
    glNamedFramebufferTexture(m_resolveFbo, GL_COLOR_ATTACHMENT0, m_resolveColor, 0);
    glNamedFramebufferTexture(m_resolveFbo, GL_DEPTH_ATTACHMENT, m_resolveDepth, 0);
    if (glCheckNamedFramebufferStatus(m_resolveFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log::error("Scene framebuffer incomplete");
    }

    if (samples > 0) {
        glCreateRenderbuffers(1, &m_msaaColor);
        glNamedRenderbufferStorageMultisample(m_msaaColor, samples, GL_RGBA16F, sceneSize.x, sceneSize.y);
        glCreateRenderbuffers(1, &m_msaaDepth);
        glNamedRenderbufferStorageMultisample(m_msaaDepth, samples, GL_DEPTH_COMPONENT32F, sceneSize.x, sceneSize.y);
        glCreateFramebuffers(1, &m_msaaFbo);
        glNamedFramebufferRenderbuffer(m_msaaFbo, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msaaColor);
        glNamedFramebufferRenderbuffer(m_msaaFbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_msaaDepth);
        if (glCheckNamedFramebufferStatus(m_msaaFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            log::error("MSAA framebuffer incomplete");
        }
    }

    const glm::ivec2 half = glm::max(sceneSize / 2, glm::ivec2(1));
    m_ssaoRaw = makeTexture2D(GL_R8, half, GL_LINEAR);
    m_ssaoFbo = makeColorFbo(m_ssaoRaw);
    m_ssaoBlurred = makeTexture2D(GL_R8, half, GL_LINEAR);
    m_ssaoBlurFbo = makeColorFbo(m_ssaoBlurred);

    glm::ivec2 mipSize = half;
    for (int i = 0; i < kBloomMips; ++i) {
        const auto index = static_cast<size_t>(i);
        m_bloomSizes[index] = mipSize;
        m_bloomTextures[index] = makeTexture2D(GL_R11F_G11F_B10F, mipSize, GL_LINEAR);
        m_bloomFbos[index] = makeColorFbo(m_bloomTextures[index]);
        mipSize = glm::max(mipSize / 2, glm::ivec2(1));
    }
    for (int i = 0; i < 2; ++i) {
        m_dofTexture[i] = makeTexture2D(GL_R11F_G11F_B10F, half, GL_LINEAR);
        m_dofFbo[i] = makeColorFbo(m_dofTexture[i]);
    }
    m_copyColor = makeTexture2D(GL_RGBA16F, sceneSize, GL_LINEAR);
    m_copyDepth = makeTexture2D(GL_DEPTH_COMPONENT32F, sceneSize, GL_NEAREST);
    glCreateFramebuffers(1, &m_copyFbo);
    glNamedFramebufferTexture(m_copyFbo, GL_COLOR_ATTACHMENT0, m_copyColor, 0);
    glNamedFramebufferTexture(m_copyFbo, GL_DEPTH_ATTACHMENT, m_copyDepth, 0);
    m_godRaysTexture = makeTexture2D(GL_R11F_G11F_B10F, half, GL_LINEAR);
    m_godRaysFbo = makeColorFbo(m_godRaysTexture);
}

void Renderer::ensureShadowMaps() {
    const ShadowConfig config = shadowConfig(m_settings.shadowQuality);
    if (config.size == m_shadowSize && config.cascades == m_cascadeCount) {
        return;
    }
    for (unsigned& fbo : m_shadowFbos) deleteFbo(fbo);
    deleteTexture(m_shadowArray);
    m_shadowSize = config.size;
    m_cascadeCount = config.cascades;
    if (config.size == 0) {
        return;
    }
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &m_shadowArray);
    glTextureStorage3D(m_shadowArray, 1, GL_DEPTH_COMPONENT32F, config.size, config.size, config.cascades);
    glTextureParameteri(m_shadowArray, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(m_shadowArray, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(m_shadowArray, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(m_shadowArray, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTextureParameterfv(m_shadowArray, GL_TEXTURE_BORDER_COLOR, border);
    glTextureParameteri(m_shadowArray, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(m_shadowArray, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    for (int i = 0; i < config.cascades; ++i) {
        unsigned& fbo = m_shadowFbos[static_cast<size_t>(i)];
        glCreateFramebuffers(1, &fbo);
        glNamedFramebufferTextureLayer(fbo, GL_DEPTH_ATTACHMENT, m_shadowArray, 0, i);
        glNamedFramebufferDrawBuffer(fbo, GL_NONE);
        glNamedFramebufferReadBuffer(fbo, GL_NONE);
    }
}

// --- Shadows --------------------------------------------------------------------------------

void Renderer::computeCascades(const CameraView& camera, const Environment& environment) {
    const ShadowConfig config = shadowConfig(m_settings.shadowQuality);
    const float nearPlane = camera.nearPlane;
    const float farPlane = std::min(config.distance, camera.farPlane);
    const float lambda = 0.8f;
    const glm::mat4 invView = glm::inverse(camera.view);
    const float aspect = static_cast<float>(m_outputSize.x) / static_cast<float>(std::max(m_outputSize.y, 1));
    const float tanHalfY = std::tan(glm::radians(camera.fovYDegrees) * 0.5f);
    const float tanHalfX = tanHalfY * aspect;

    const glm::vec3 lightDir = glm::normalize(environment.sunDirection);
    const glm::vec3 up = std::abs(lightDir.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    const glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), -lightDir, up);

    float sliceNear = nearPlane;
    for (int c = 0; c < m_cascadeCount; ++c) {
        const float p = static_cast<float>(c + 1) / static_cast<float>(m_cascadeCount);
        const float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);
        const float uniformSplit = nearPlane + (farPlane - nearPlane) * p;
        const float sliceFar = lambda * logSplit + (1.0f - lambda) * uniformSplit;

        // Bounding sphere of the frustum slice (stable under camera rotation).
        glm::vec3 corners[8];
        int k = 0;
        for (float d : {sliceNear, sliceFar}) {
            for (float sx : {-1.0f, 1.0f}) {
                for (float sy : {-1.0f, 1.0f}) {
                    corners[k++] = glm::vec3(invView * glm::vec4(sx * tanHalfX * d, sy * tanHalfY * d, -d, 1.0f));
                }
            }
        }
        glm::vec3 center(0.0f);
        for (const glm::vec3& corner : corners) center += corner;
        center /= 8.0f;
        float radius = 0.0f;
        for (const glm::vec3& corner : corners) radius = std::max(radius, glm::length(corner - center));
        radius = std::ceil(radius * 16.0f) / 16.0f;

        glm::vec3 lightCenter = glm::vec3(lightView * glm::vec4(center, 1.0f));
        const float texel = (2.0f * radius) / static_cast<float>(m_shadowSize);
        lightCenter.x = std::floor(lightCenter.x / texel) * texel;
        lightCenter.y = std::floor(lightCenter.y / texel) * texel;
        const float depthRange = 250.0f;
        const glm::mat4 lightProj = glm::ortho(lightCenter.x - radius, lightCenter.x + radius, lightCenter.y - radius,
                                               lightCenter.y + radius, -lightCenter.z - depthRange,
                                               -lightCenter.z + radius + 20.0f);
        const auto index = static_cast<size_t>(c);
        m_cascadeViewProj[index] = lightProj * lightView;
        m_cascadeSplits[index] = sliceFar;
        m_cascadeTexel[index] = texel;
        m_cascadeDepthRange[index] = depthRange + radius + 20.0f;
        sliceNear = sliceFar;
    }
}

// --- Passes ---------------------------------------------------------------------------------

Shader& Renderer::litVariant(const DrawItem& item) {
    if (item.instanceCount > 0) {
        return item.material.alphaTest ? m_litFoliage : m_litInstanced;
    }
    return m_litShader;
}

void Renderer::uploadInstances() {
    m_instanceStaging.clear();
    m_itemInstanceOffsets.assign(m_items.size(), 0);
    for (size_t i = 0; i < m_items.size(); ++i) {
        const DrawItem& item = m_items[i];
        if (item.instanceCount > 0 && item.instances) {
            m_itemInstanceOffsets[i] = static_cast<int>(m_instanceStaging.size());
            m_instanceStaging.insert(m_instanceStaging.end(), item.instances, item.instances + item.instanceCount);
        }
    }
    if (m_instanceStaging.empty()) {
        return;
    }
    const size_t bytes = m_instanceStaging.size() * sizeof(glm::mat4);
    if (bytes > m_instanceCapacity) {
        if (m_instanceBuffer) glDeleteBuffers(1, &m_instanceBuffer);
        m_instanceCapacity = std::max(bytes, m_instanceCapacity * 2);
        glCreateBuffers(1, &m_instanceBuffer);
        glNamedBufferData(m_instanceBuffer, static_cast<GLsizeiptr>(m_instanceCapacity), nullptr, GL_DYNAMIC_DRAW);
    }
    glNamedBufferSubData(m_instanceBuffer, 0, static_cast<GLsizeiptr>(bytes), m_instanceStaging.data());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_instanceBuffer);
}

void Renderer::drawDepth(const std::vector<DrawItem>& items, const glm::mat4& viewProj, bool shadowPass, float time) {
    Shader* current = nullptr;
    for (size_t i = 0; i < items.size(); ++i) {
        const DrawItem& item = items[i];
        if (!item.mesh || (shadowPass && !item.castShadow)) {
            continue;
        }
        Shader& shader = item.instanceCount > 0 ? (item.material.alphaTest ? m_depthFoliage : m_depthInstanced) : m_depthShader;
        if (&shader != current) {
            current = &shader;
            shader.bind();
            shader.set("uViewProj", viewProj);
            shader.set("uTime", time);
            shader.set("uAlbedoMap", 2);
        }
        const bool twoSided = item.material.alphaTest;
        if (twoSided) glDisable(GL_CULL_FACE);
        if (item.instanceCount > 0) {
            shader.set("uInstanceOffset", m_itemInstanceOffsets[i]);
            shader.set("uWindStrength", item.material.windStrength);
            shader.set("uWindHeight", item.material.windHeight);
            if (item.material.alphaTest) glBindTextureUnit(2, item.material.albedoMap);
            item.mesh->drawInstanced(item.instanceCount);
        } else {
            shader.set("uModel", item.model);
            item.mesh->draw();
        }
        if (twoSided) glEnable(GL_CULL_FACE);
        m_stats.drawCalls++;
    }
}

void Renderer::setLitFrameUniforms(Shader& shader, const CameraView& camera, const glm::mat4& viewProj,
                                   const Environment& environment, bool shadowsOn) {
    shader.set("uViewProj", viewProj);
    shader.set("uView", camera.view);
    shader.set("uCameraPos", camera.position);
    shader.set("uTime", environment.time);
    shader.set("uSunDirection", glm::normalize(environment.sunDirection));
    shader.set("uSunColor", environment.sunColor * environment.sunIntensity);
    shader.set("uSkyColor", environment.skyColor);
    shader.set("uGroundColor", environment.groundColor);
    shader.set("uAmbientIntensity", environment.ambientIntensity);
    shader.set("uFogColor", environment.fogColor);
    shader.set("uFogDensity", environment.fogDensity);
    shader.set("uShadowsEnabled", shadowsOn ? 1 : 0);
    shader.set("uCascadeCount", m_cascadeCount);
    shader.set("uPcfRadius", shadowConfig(m_settings.shadowQuality).pcfRadius);
    shader.set("uShadowTexel", shadowsOn ? 1.0f / static_cast<float>(m_shadowSize) : 0.0f);
    for (int c = 0; c < m_cascadeCount; ++c) {
        const std::string index = "[" + std::to_string(c) + "]";
        shader.set(("uCascadeViewProj" + index).c_str(), m_cascadeViewProj[static_cast<size_t>(c)]);
        shader.set(("uCascadeSplit" + index).c_str(), m_cascadeSplits[static_cast<size_t>(c)]);
        shader.set(("uCascadeTexel" + index).c_str(), m_cascadeTexel[static_cast<size_t>(c)]);
        shader.set(("uCascadeDepthRange" + index).c_str(), m_cascadeDepthRange[static_cast<size_t>(c)]);
    }
    shader.set("uShadowMap", 0);
    shader.set("uShadowRaw", 8);
    shader.set("uSoftShadows", m_settings.shadowQuality >= 3 ? 1 : 0);
    shader.set("uSceneDepth", 9);
    shader.set("uProj", projection(camera));
    shader.set("uContactShadows", m_settings.contactShadows ? 1 : 0);
    shader.set("uAo", 1);
    shader.set("uAlbedoMap", 2);
    shader.set("uNormalMap", 3);
    shader.set("uArmMap", 4);
    shader.set("uTerrainAlbedo", 5);
    shader.set("uTerrainNormal", 6);
    shader.set("uTerrainArm", 7);
    shader.set("uAoEnabled", m_settings.ssao ? 1 : 0);
    shader.set("uSceneSize", glm::vec2(m_sceneSize));
    const int lightCount = static_cast<int>(m_lights.size());
    shader.set("uLightCount", lightCount);
    for (int i = 0; i < lightCount; ++i) {
        const PointLight& light = m_lights[static_cast<size_t>(i)];
        const std::string index = "[" + std::to_string(i) + "]";
        shader.set(("uLightPosRadius" + index).c_str(), glm::vec4(light.position, light.radius));
        shader.set(("uLightColor" + index).c_str(), light.color * light.intensity);
    }
}

void Renderer::drawLit(const std::vector<DrawItem>& items, const glm::mat4& toWorld, const std::vector<int>& offsets) {
    Shader* current = nullptr;
    const bool alphaToCoverage = m_targetSamples > 0;
    for (size_t i = 0; i < items.size(); ++i) {
        const DrawItem& item = items[i];
        if (!item.mesh) {
            continue;
        }
        Shader& shader = litVariant(item);
        if (&shader != current) {
            current = &shader;
            shader.bind();
        }
        const Material& m = item.material;
        shader.set("uAlbedo", m.albedo);
        shader.set("uRoughness", m.roughness);
        shader.set("uMetallic", m.metallic);
        shader.set("uEmissive", m.emissive);
        shader.set("uGridSize", m.gridSize);
        shader.set("uShading", m.shading);
        shader.set("uUvScale", m.uvScale);
        shader.set("uHasAlbedoMap", m.albedoMap ? 1 : 0);
        shader.set("uHasNormalMap", m.normalMap ? 1 : 0);
        shader.set("uHasArmMap", m.armMap ? 1 : 0);
        glBindTextureUnit(2, m.albedoMap);
        glBindTextureUnit(3, m.normalMap);
        glBindTextureUnit(4, m.armMap);
        if (m.shading == 1) {
            shader.set("uHasTerrainTextures", m.terrainAlbedo ? 1 : 0);
            shader.set("uWaterLevel", m.waterLevel);
            glBindTextureUnit(5, m.terrainAlbedo);
            glBindTextureUnit(6, m.terrainNormal);
            glBindTextureUnit(7, m.terrainArm);
        }
        if (m.alphaTest) {
            glDisable(GL_CULL_FACE);
            if (alphaToCoverage) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        }
        if (item.instanceCount > 0) {
            shader.set("uInstanceOffset", i < offsets.size() ? offsets[i] : 0);
            shader.set("uWindStrength", m.windStrength);
            shader.set("uWindHeight", m.windHeight);
            item.mesh->drawInstanced(item.instanceCount);
        } else {
            shader.set("uModel", toWorld * item.model);
            item.mesh->draw();
        }
        if (m.alphaTest) {
            glEnable(GL_CULL_FACE);
            glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        }
        m_stats.drawCalls++;
    }
}

void Renderer::drawLines(const std::vector<LineVertex>& lines, const glm::mat4& viewProj) {
    if (lines.empty()) {
        return;
    }
    const size_t bytes = lines.size() * sizeof(LineVertex);
    if (bytes > m_lineCapacity) {
        if (m_lineVbo) glDeleteBuffers(1, &m_lineVbo);
        m_lineCapacity = std::max(bytes, m_lineCapacity * 2);
        glCreateBuffers(1, &m_lineVbo);
        glNamedBufferData(m_lineVbo, static_cast<GLsizeiptr>(m_lineCapacity), nullptr, GL_DYNAMIC_DRAW);
        glVertexArrayVertexBuffer(m_lineVao, 0, m_lineVbo, 0, sizeof(LineVertex));
    }
    glNamedBufferSubData(m_lineVbo, 0, static_cast<GLsizeiptr>(bytes), lines.data());
    m_lineShader.bind();
    m_lineShader.set("uViewProj", viewProj);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(m_lineVao);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size()));
    glDisable(GL_BLEND);
    m_stats.drawCalls++;
}

void Renderer::renderSsao(const glm::mat4& proj) {
    const glm::ivec2 half = m_bloomSizes[0];
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFbo);
    glViewport(0, 0, half.x, half.y);
    m_ssaoShader.bind();
    m_ssaoShader.set("uProj", proj);
    m_ssaoShader.set("uInvProj", glm::inverse(proj));
    m_ssaoShader.set("uDepthTexel", 1.0f / glm::vec2(m_sceneSize));
    m_ssaoShader.set("uDepth", 0);
    glBindTextureUnit(0, m_resolveDepth);
    drawFullscreen(m_emptyVao);

    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoBlurFbo);
    m_ssaoBlurShader.bind();
    m_ssaoBlurShader.set("uInput", 0);
    m_ssaoBlurShader.set("uDepth", 1);
    m_ssaoBlurShader.set("uTexel", 1.0f / glm::vec2(half));
    glBindTextureUnit(0, m_ssaoRaw);
    glBindTextureUnit(1, m_resolveDepth);
    drawFullscreen(m_emptyVao);
    m_stats.drawCalls += 2;
}

void Renderer::renderBloom() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    m_bloomDownShader.bind();
    m_bloomDownShader.set("uSource", 0);
    for (int i = 0; i < kBloomMips; ++i) {
        const auto index = static_cast<size_t>(i);
        const unsigned source = i == 0 ? m_resolveColor : m_bloomTextures[index - 1];
        const glm::ivec2 sourceSize = i == 0 ? m_sceneSize : m_bloomSizes[index - 1];
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbos[index]);
        glViewport(0, 0, m_bloomSizes[index].x, m_bloomSizes[index].y);
        m_bloomDownShader.set("uSourceTexel", 1.0f / glm::vec2(sourceSize));
        m_bloomDownShader.set("uFirstPass", i == 0 ? 1 : 0);
        glBindTextureUnit(0, source);
        drawFullscreen(m_emptyVao);
    }
    m_bloomUpShader.bind();
    m_bloomUpShader.set("uSource", 0);
    m_bloomUpShader.set("uRadius", 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = kBloomMips - 1; i > 0; --i) {
        const auto index = static_cast<size_t>(i);
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFbos[index - 1]);
        glViewport(0, 0, m_bloomSizes[index - 1].x, m_bloomSizes[index - 1].y);
        m_bloomUpShader.set("uSourceTexel", 1.0f / glm::vec2(m_bloomSizes[index]));
        glBindTextureUnit(0, m_bloomTextures[index]);
        drawFullscreen(m_emptyVao);
    }
    glDisable(GL_BLEND);
    m_stats.drawCalls += kBloomMips * 2 - 1;
}

void Renderer::renderDofBlur() {
    // Half-resolution separable blur of the scene for the depth of field.
    const glm::ivec2 half = m_bloomSizes[0];
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    m_blurShader.bind();
    m_blurShader.set("uSource", 0);
    for (int pass = 0; pass < 2; ++pass) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_dofFbo[pass]);
        glViewport(0, 0, half.x, half.y);
        m_blurShader.set("uDirection", pass == 0 ? glm::vec2(1.0f / static_cast<float>(half.x), 0.0f)
                                                 : glm::vec2(0.0f, 1.0f / static_cast<float>(half.y)));
        glBindTextureUnit(0, pass == 0 ? m_resolveColor : m_dofTexture[0]);
        drawFullscreen(m_emptyVao);
    }
    m_stats.drawCalls += 2;
}

void Renderer::renderVolumetric(const CameraView& camera, const glm::mat4& viewProj, const Environment& environment) {
    const glm::ivec2 half = m_bloomSizes[0];
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindFramebuffer(GL_FRAMEBUFFER, m_godRaysFbo);
    glViewport(0, 0, half.x, half.y);
    m_volumetricShader.bind();
    m_volumetricShader.set("uDepth", 0);
    m_volumetricShader.set("uShadowMap", 1);
    m_volumetricShader.set("uInvViewProj", glm::inverse(viewProj));
    m_volumetricShader.set("uView", camera.view);
    m_volumetricShader.set("uCameraPos", camera.position);
    m_volumetricShader.set("uSunDirection", glm::normalize(environment.sunDirection));
    m_volumetricShader.set("uSunColor", environment.sunColor * environment.sunIntensity);
    m_volumetricShader.set("uDensity", environment.volumetricDensity);
    m_volumetricShader.set("uGroundHeight", environment.groundHeight);
    m_volumetricShader.set("uMaxDistance", 90.0f);
    m_volumetricShader.set("uTime", environment.time);
    m_volumetricShader.set("uCascadeCount", m_cascadeCount);
    for (int c = 0; c < m_cascadeCount; ++c) {
        const std::string index = "[" + std::to_string(c) + "]";
        m_volumetricShader.set(("uCascadeViewProj" + index).c_str(), m_cascadeViewProj[static_cast<size_t>(c)]);
        m_volumetricShader.set(("uCascadeSplit" + index).c_str(), m_cascadeSplits[static_cast<size_t>(c)]);
    }
    glBindTextureUnit(0, m_resolveDepth);
    glBindTextureUnit(1, m_shadowArray);
    drawFullscreen(m_emptyVao);

    // Blur away the per-pixel jitter: horizontal into a scratch texture, vertical back.
    m_blurShader.bind();
    m_blurShader.set("uSource", 0);
    glBindFramebuffer(GL_FRAMEBUFFER, m_dofFbo[0]);
    m_blurShader.set("uDirection", glm::vec2(1.0f / static_cast<float>(half.x), 0.0f));
    glBindTextureUnit(0, m_godRaysTexture);
    drawFullscreen(m_emptyVao);
    glBindFramebuffer(GL_FRAMEBUFFER, m_godRaysFbo);
    m_blurShader.set("uDirection", glm::vec2(0.0f, 1.0f / static_cast<float>(half.y)));
    glBindTextureUnit(0, m_dofTexture[0]);
    drawFullscreen(m_emptyVao);
    m_stats.drawCalls += 3;
}

void Renderer::renderGodRays(const CameraView& camera, const glm::mat4& viewProj, const Environment& environment) {
    const glm::vec3 sunDir = glm::normalize(environment.sunDirection);
    const glm::vec3 forward = -glm::vec3(glm::inverse(camera.view)[2]);
    const float facing = glm::dot(forward, sunDir);
    glBindFramebuffer(GL_FRAMEBUFFER, m_godRaysFbo);
    glViewport(0, 0, m_bloomSizes[0].x, m_bloomSizes[0].y);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (facing <= 0.0f) {
        return;
    }
    const glm::vec4 sunClip = viewProj * glm::vec4(camera.position + sunDir * 1000.0f, 1.0f);
    const glm::vec2 sunUv = glm::vec2(sunClip) / sunClip.w * 0.5f + 0.5f;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    m_godRaysShader.bind();
    m_godRaysShader.set("uDepth", 0);
    m_godRaysShader.set("uSunUv", sunUv);
    m_godRaysShader.set("uAspect", static_cast<float>(m_sceneSize.x) / static_cast<float>(m_sceneSize.y));
    m_godRaysShader.set("uSunColor", environment.sunColor * environment.sunIntensity);
    m_godRaysShader.set("uIntensity", std::pow(facing, 4.0f) * 0.12f);
    glBindTextureUnit(0, m_resolveDepth);
    drawFullscreen(m_emptyVao);
    m_stats.drawCalls++;
}

void Renderer::renderWater(const CameraView& camera, const glm::mat4& viewProj, const Environment& environment, unsigned sceneFbo) {
    if (m_transparentItems.empty()) {
        return;
    }
    // Copy the opaque scene so the water can refract it without a feedback loop.
    glBlitNamedFramebuffer(sceneFbo, m_copyFbo, 0, 0, m_sceneSize.x, m_sceneSize.y, 0, 0, m_sceneSize.x, m_sceneSize.y,
                           GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBlitNamedFramebuffer(sceneFbo, m_copyFbo, 0, 0, m_sceneSize.x, m_sceneSize.y, 0, 0, m_sceneSize.x, m_sceneSize.y,
                           GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
    glDisable(GL_CULL_FACE);
    m_waterShader.bind();
    m_waterShader.set("uViewProj", viewProj);
    m_waterShader.set("uCameraPos", camera.position);
    m_waterShader.set("uTime", environment.time);
    m_waterShader.set("uSunDirection", glm::normalize(environment.sunDirection));
    m_waterShader.set("uSunColor", environment.sunColor * environment.sunIntensity);
    m_waterShader.set("uSkyColor", environment.skyColor);
    m_waterShader.set("uFogColor", environment.fogColor);
    m_waterShader.set("uFogDensity", environment.fogDensity);
    m_waterShader.set("uSceneSize", glm::vec2(m_sceneSize));
    m_waterShader.set("uNear", camera.nearPlane);
    m_waterShader.set("uView", camera.view);
    m_waterShader.set("uProj", projection(camera));
    m_waterShader.set("uSsrEnabled", m_settings.ssr ? 1 : 0);
    m_waterShader.set("uFar", camera.farPlane);
    m_waterShader.set("uSceneColor", 0);
    m_waterShader.set("uSceneDepth", 1);
    glBindTextureUnit(0, m_copyColor);
    glBindTextureUnit(1, m_copyDepth);
    for (const DrawItem& item : m_transparentItems) {
        m_waterShader.set("uModel", item.model);
        item.mesh->draw();
        m_stats.drawCalls++;
    }
    glEnable(GL_CULL_FACE);
}

void Renderer::render(const CameraView& camera, const Environment& environment) {
    const glm::mat4 proj = projection(camera);
    const glm::mat4 viewProj = proj * camera.view;
    const bool shadowsOn = m_shadowSize > 0;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);

    uploadInstances();

    // --- Cascaded shadow maps ----------------------------------------------------------
    if (shadowsOn) {
        computeCascades(camera, environment);
        glViewport(0, 0, m_shadowSize, m_shadowSize);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 2.0f);
        for (int c = 0; c < m_cascadeCount; ++c) {
            glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbos[static_cast<size_t>(c)]);
            glClear(GL_DEPTH_BUFFER_BIT);
            drawDepth(m_items, m_cascadeViewProj[static_cast<size_t>(c)], true, environment.time);
        }
        glDisable(GL_POLYGON_OFFSET_FILL);
        m_stats.shadowCascades = m_cascadeCount;
    }

    // --- Depth prepass ---------------------------------------------------------------------
    const unsigned sceneFbo = m_targetSamples > 0 ? m_msaaFbo : m_resolveFbo;
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
    glViewport(0, 0, m_sceneSize.x, m_sceneSize.y);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawDepth(m_items, viewProj, false, environment.time);
    if (m_targetSamples > 0) {
        glBlitNamedFramebuffer(m_msaaFbo, m_resolveFbo, 0, 0, m_sceneSize.x, m_sceneSize.y, 0, 0, m_sceneSize.x,
                               m_sceneSize.y, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    }

    // --- SSAO from the prepass depth --------------------------------------------------------
    if (m_settings.ssao) {
        renderSsao(proj);
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
        glViewport(0, 0, m_sceneSize.x, m_sceneSize.y);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
    }

    // --- Sky -------------------------------------------------------------------------------
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LEQUAL);
    m_skyShader.bind();
    m_skyShader.set("uInvViewProj", glm::inverse(viewProj));
    m_skyShader.set("uCameraPos", camera.position);
    m_skyShader.set("uSunDirection", glm::normalize(environment.sunDirection));
    m_skyShader.set("uSunColor", environment.sunColor * environment.sunIntensity);
    m_skyShader.set("uSkyColor", environment.skyColor);
    m_skyShader.set("uFogColor", environment.fogColor);
    drawFullscreen(m_emptyVao);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);

    // --- Opaque lit pass ------------------------------------------------------------------
    setLitFrameUniforms(m_litShader, camera, viewProj, environment, shadowsOn);
    setLitFrameUniforms(m_litInstanced, camera, viewProj, environment, shadowsOn);
    setLitFrameUniforms(m_litFoliage, camera, viewProj, environment, shadowsOn);
    glBindTextureUnit(0, shadowsOn ? m_shadowArray : 0);
    glBindTextureUnit(1, m_settings.ssao ? m_ssaoBlurred : m_whiteTexture);
    glBindTextureUnit(8, shadowsOn ? m_shadowArray : 0);
    glBindSampler(8, m_shadowRawSampler);
    glBindTextureUnit(9, m_resolveDepth);

    drawLit(m_items, glm::mat4(1.0f), m_itemInstanceOffsets);
    renderWater(camera, viewProj, environment, sceneFbo);
    glBindTextureUnit(0, shadowsOn ? m_shadowArray : 0);
    glBindTextureUnit(1, m_settings.ssao ? m_ssaoBlurred : m_whiteTexture);
    drawLines(m_lines, viewProj);

    // View model: own projection, cleared depth so it never clips into walls. No SSAO.
    if (!m_viewModelItems.empty()) {
        glClear(GL_DEPTH_BUFFER_BIT);
        CameraView vmCamera = camera;
        vmCamera.fovYDegrees = m_settings.viewModelFov;
        vmCamera.nearPlane = 0.01f;
        vmCamera.farPlane = 10.0f;
        m_litShader.bind();
        m_litShader.set("uViewProj", projection(vmCamera) * camera.view);
        m_litShader.set("uAoEnabled", 0);
        drawLit(m_viewModelItems, glm::inverse(camera.view), m_noOffsets);
        m_litShader.set("uAoEnabled", m_settings.ssao ? 1 : 0);
    }

    glDisable(GL_DEPTH_TEST);
    drawLines(m_overlayLines, viewProj);
    glEnable(GL_DEPTH_TEST);

    if (m_targetSamples > 0) {
        glBlitNamedFramebuffer(m_msaaFbo, m_resolveFbo, 0, 0, m_sceneSize.x, m_sceneSize.y, 0, 0, m_sceneSize.x,
                               m_sceneSize.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    // --- Post processing ------------------------------------------------------------------
    if (m_settings.bloom) {
        renderBloom();
    }
    if (m_settings.godRays) {
        if (m_shadowSize > 0) {
            renderVolumetric(camera, viewProj, environment);
        } else {
            renderGodRays(camera, viewProj, environment);
        }
    }
    if (m_settings.depthOfField > 0.001f) {
        renderDofBlur();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_outputFbo);
    glViewport(0, 0, m_outputSize.x, m_outputSize.y);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    m_compositeShader.bind();
    m_compositeShader.set("uExposure", m_settings.exposure);
    m_compositeShader.set("uBloomStrength", m_settings.bloom ? m_settings.bloomStrength : 0.0f);
    m_compositeShader.set("uDof", m_settings.depthOfField);
    m_compositeShader.set("uFocus", m_settings.focusDistance);
    m_compositeShader.set("uNear", camera.nearPlane);
    m_compositeShader.set("uFar", camera.farPlane);
    m_compositeShader.set("uDepth", 3);
    m_compositeShader.set("uBlurred", 4);
    glBindTextureUnit(3, m_resolveDepth);
    glBindTextureUnit(4, m_settings.depthOfField > 0.001f ? m_dofTexture[1] : m_blackTexture);
    m_compositeShader.set("uScene", 0);
    m_compositeShader.set("uBloom", 1);
    m_compositeShader.set("uGodRays", 2);
    glBindTextureUnit(0, m_resolveColor);
    glBindTextureUnit(1, m_settings.bloom ? m_bloomTextures[0] : m_blackTexture);
    glBindTextureUnit(2, m_settings.godRays ? m_godRaysTexture : m_blackTexture);
    drawFullscreen(m_emptyVao);
    for (unsigned unit = 0; unit < 5; ++unit) {
        glBindTextureUnit(unit, 0);
    }
    m_stats.drawCalls++;
}

} // namespace origon
