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
    ok &= m_depthShader.loadFile("assets/shaders/depth.glsl");
    ok &= m_skyShader.loadFile("assets/shaders/sky.glsl");
    ok &= m_compositeShader.loadFile("assets/shaders/composite.glsl");
    ok &= m_ssaoShader.loadFile("assets/shaders/ssao.glsl");
    ok &= m_ssaoBlurShader.loadFile("assets/shaders/ssao_blur.glsl");
    ok &= m_bloomDownShader.loadFile("assets/shaders/bloom_down.glsl");
    ok &= m_bloomUpShader.loadFile("assets/shaders/bloom_up.glsl");
    ok &= m_godRaysShader.loadFile("assets/shaders/godrays.glsl");
    ok &= m_lineShader.loadFile("assets/shaders/lines.glsl");
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
        sliceNear = sliceFar;
    }
}

// --- Passes ---------------------------------------------------------------------------------

void Renderer::drawLit(const std::vector<DrawItem>& items, const glm::mat4& toWorld) {
    for (const DrawItem& item : items) {
        if (!item.mesh) {
            continue;
        }
        m_litShader.set("uModel", toWorld * item.model);
        m_litShader.set("uAlbedo", item.material.albedo);
        m_litShader.set("uRoughness", item.material.roughness);
        m_litShader.set("uMetallic", item.material.metallic);
        m_litShader.set("uEmissive", item.material.emissive);
        m_litShader.set("uGridSize", item.material.gridSize);
        item.mesh->draw();
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
    m_godRaysShader.set("uIntensity", std::pow(facing, 3.0f) * 0.35f);
    glBindTextureUnit(0, m_resolveDepth);
    drawFullscreen(m_emptyVao);
    m_stats.drawCalls++;
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

    // --- Cascaded shadow maps ----------------------------------------------------------
    if (shadowsOn) {
        computeCascades(camera, environment);
        glViewport(0, 0, m_shadowSize, m_shadowSize);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 2.0f);
        m_depthShader.bind();
        for (int c = 0; c < m_cascadeCount; ++c) {
            glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFbos[static_cast<size_t>(c)]);
            glClear(GL_DEPTH_BUFFER_BIT);
            m_depthShader.set("uViewProj", m_cascadeViewProj[static_cast<size_t>(c)]);
            for (const DrawItem& item : m_items) {
                if (item.castShadow && item.mesh) {
                    m_depthShader.set("uModel", item.model);
                    item.mesh->draw();
                    m_stats.drawCalls++;
                }
            }
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
    m_depthShader.bind();
    m_depthShader.set("uViewProj", viewProj);
    for (const DrawItem& item : m_items) {
        if (item.mesh) {
            m_depthShader.set("uModel", item.model);
            item.mesh->draw();
            m_stats.drawCalls++;
        }
    }
    if (m_targetSamples > 0) {
        glBlitNamedFramebuffer(m_msaaFbo, m_resolveFbo, 0, 0, m_sceneSize.x, m_sceneSize.y, 0, 0, m_sceneSize.x,
                               m_sceneSize.y, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    }

    // --- SSAO from the prepass depth --------------------------------------------------------
    if (m_settings.ssao) {
        renderSsao(proj);
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
        glViewport(0, 0, m_sceneSize.x, m_sceneSize.y);
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
    m_litShader.bind();
    m_litShader.set("uViewProj", viewProj);
    m_litShader.set("uView", camera.view);
    m_litShader.set("uCameraPos", camera.position);
    m_litShader.set("uSunDirection", glm::normalize(environment.sunDirection));
    m_litShader.set("uSunColor", environment.sunColor * environment.sunIntensity);
    m_litShader.set("uSkyColor", environment.skyColor);
    m_litShader.set("uGroundColor", environment.groundColor);
    m_litShader.set("uAmbientIntensity", environment.ambientIntensity);
    m_litShader.set("uFogColor", environment.fogColor);
    m_litShader.set("uFogDensity", environment.fogDensity);
    m_litShader.set("uShadowsEnabled", shadowsOn ? 1 : 0);
    m_litShader.set("uCascadeCount", m_cascadeCount);
    m_litShader.set("uPcfRadius", shadowConfig(m_settings.shadowQuality).pcfRadius);
    m_litShader.set("uShadowTexel", shadowsOn ? 1.0f / static_cast<float>(m_shadowSize) : 0.0f);
    for (int c = 0; c < m_cascadeCount; ++c) {
        const std::string index = "[" + std::to_string(c) + "]";
        m_litShader.set(("uCascadeViewProj" + index).c_str(), m_cascadeViewProj[static_cast<size_t>(c)]);
        m_litShader.set(("uCascadeSplit" + index).c_str(), m_cascadeSplits[static_cast<size_t>(c)]);
        m_litShader.set(("uCascadeTexel" + index).c_str(), m_cascadeTexel[static_cast<size_t>(c)]);
    }
    m_litShader.set("uShadowMap", 0);
    m_litShader.set("uAo", 1);
    m_litShader.set("uAoEnabled", m_settings.ssao ? 1 : 0);
    m_litShader.set("uSceneSize", glm::vec2(m_sceneSize));
    glBindTextureUnit(0, shadowsOn ? m_shadowArray : 0);
    glBindTextureUnit(1, m_settings.ssao ? m_ssaoBlurred : m_whiteTexture);

    const int lightCount = static_cast<int>(m_lights.size());
    m_litShader.set("uLightCount", lightCount);
    for (int i = 0; i < lightCount; ++i) {
        const PointLight& light = m_lights[static_cast<size_t>(i)];
        const std::string index = "[" + std::to_string(i) + "]";
        m_litShader.set(("uLightPosRadius" + index).c_str(), glm::vec4(light.position, light.radius));
        m_litShader.set(("uLightColor" + index).c_str(), light.color * light.intensity);
    }

    drawLit(m_items, glm::mat4(1.0f));
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
        drawLit(m_viewModelItems, glm::inverse(camera.view));
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
        renderGodRays(camera, viewProj, environment);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_outputFbo);
    glViewport(0, 0, m_outputSize.x, m_outputSize.y);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    m_compositeShader.bind();
    m_compositeShader.set("uExposure", m_settings.exposure);
    m_compositeShader.set("uBloomStrength", m_settings.bloom ? m_settings.bloomStrength : 0.0f);
    m_compositeShader.set("uScene", 0);
    m_compositeShader.set("uBloom", 1);
    m_compositeShader.set("uGodRays", 2);
    glBindTextureUnit(0, m_resolveColor);
    glBindTextureUnit(1, m_settings.bloom ? m_bloomTextures[0] : m_blackTexture);
    glBindTextureUnit(2, m_settings.godRays ? m_godRaysTexture : m_blackTexture);
    drawFullscreen(m_emptyVao);
    for (unsigned unit = 0; unit < 3; ++unit) {
        glBindTextureUnit(unit, 0);
    }
    m_stats.drawCalls++;
}

} // namespace origon
