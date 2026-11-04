#pragma once

#include "render/Mesh.h"
#include "render/Shader.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <unordered_map>
#include <vector>

namespace origon {

struct Material {
    glm::vec3 albedo{0.7f};
    float roughness = 0.8f;
    glm::vec3 emissive{0.0f};
    float metallic = 0.0f;
    // World-space grid line spacing in meters for graybox surfaces. 0 disables the grid.
    float gridSize = 0.0f;
    // 0 standard, 1 terrain (procedural splat by slope, height and paint in uv.x).
    int shading = 0;
    // Texture maps (0 = none): albedo (sRGB, alpha = coverage), normal (OpenGL), ARM (AO, rough, metal).
    unsigned albedoMap = 0;
    unsigned normalMap = 0;
    unsigned armMap = 0;
    bool alphaTest = false; // Cutout foliage: alpha test, two-sided, wind, translucency.
    float windStrength = 0.0f;
    float windHeight = 1.0f; // Height at which wind bending reaches full strength.
    float uvScale = 1.0f;
    // Terrain splat layers (texture arrays): grass, needles, leaves, path, rock, creek bed.
    unsigned terrainAlbedo = 0;
    unsigned terrainNormal = 0;
    unsigned terrainArm = 0;
    float waterLevel = -1000.0f;
};

struct DrawItem {
    const Mesh* mesh = nullptr;
    glm::mat4 model{1.0f};
    Material material;
    bool castShadow = true;
    // Shadow cascades this item is drawn into (nearest first). Small detail can skip the far ones.
    int shadowCascades = 4;
    // Instanced draw: per-instance model matrices (model is ignored). Must stay valid until render().
    const glm::mat4* instances = nullptr;
    int instanceCount = 0;
    // Skinned draw: joint matrices (model space). Must stay valid until render().
    const glm::mat4* bones = nullptr;
    int boneCount = 0;
};

struct PointLight {
    glm::vec3 position{0.0f};
    float radius = 10.0f;
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
};

struct Environment {
    glm::vec3 sunDirection{0.4f, 0.8f, 0.3f}; // Points toward the sun.
    glm::vec3 sunColor{1.0f, 0.93f, 0.82f};
    float sunIntensity = 3.0f;
    glm::vec3 skyColor{0.45f, 0.6f, 0.8f};
    glm::vec3 groundColor{0.25f, 0.22f, 0.18f};
    float ambientIntensity = 0.6f;
    glm::vec3 fogColor{0.62f, 0.7f, 0.78f};
    float fogDensity = 0.008f;
    float time = 0.0f; // Seconds, for animated materials (water, wind).
    float groundHeight = 0.0f; // Reference height for the volumetric fog falloff.
    float volumetricDensity = 0.012f;
};

struct CameraView {
    glm::mat4 view{1.0f};
    glm::vec3 position{0.0f};
    float fovYDegrees = 75.0f;
    float nearPlane = 0.05f;
    float farPlane = 600.0f;
};

// Quality settings. They change image quality only, never what exists in the world.
struct RenderSettings {
    int shadowQuality = 3; // 0 off, 1 low, 2 medium, 3 high, 4 ultra
    int msaaSamples = 4;   // 0, 2, 4, 8
    float renderScale = 1.0f;
    float exposure = 1.0f;
    float viewModelFov = 60.0f;
    bool ssao = true;
    bool bloom = true;
    bool godRays = true;
    bool ssr = true; // Screen-space reflections on water.
    bool contactShadows = true;
    float depthOfField = 0.0f; // 0..1 background blur strength (set by the game, e.g. while aiming).
    float focusDistance = 10.0f;
    float bloomStrength = 0.06f;
};

// GPU time per render section (ms), measured with timestamp queries a few frames late.
enum class GpuSection { Shadows, Prepass, Ssao, Opaque, Water, Post, Count };
constexpr const char* kGpuSectionNames[] = {"Sombras", "Prepass", "SSAO", "Opacos", "Agua", "Post"};

struct RenderStats {
    int drawCalls = 0;
    int shadowCascades = 0;
    std::array<float, static_cast<size_t>(GpuSection::Count)> gpuMs{};
    float gpuTotalMs = 0.0f;
};

class Renderer {
public:
    static constexpr int kMaxPointLights = 32;
    static constexpr int kMaxCascades = 4;
    static constexpr int kBloomMips = 6;

    bool init();

    void beginFrame(const RenderSettings& settings, glm::ivec2 framebufferSize);
    void submit(const DrawItem& item) { m_items.push_back(item); }
    // View-model items use a model matrix in view space (first-person weapon).
    void submitViewModel(const DrawItem& item) { m_viewModelItems.push_back(item); }
    // Water surfaces: drawn after opaque geometry with access to the scene color and depth.
    void submitTransparent(const DrawItem& item) { m_transparentItems.push_back(item); }
    void addLight(const PointLight& light);
    // Lines are drawn in HDR after opaque geometry. Overlay lines ignore depth.
    void addLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color, bool overlay = false);
    void render(const CameraView& camera, const Environment& environment);

    const Mesh& cube() const { return m_cube; }
    const Mesh& capsule() const { return m_capsule; }
    const Mesh& sphere() const { return m_sphere; }
    glm::mat4 projection(const CameraView& camera) const;
    const RenderStats& stats() const { return m_stats; }
    // Framebuffer that receives the final image (0 = window).
    void setOutputFramebuffer(unsigned fbo) { m_outputFbo = fbo; }

private:
    struct LineVertex {
        glm::vec3 position;
        glm::vec4 color;
    };

    void ensureTargets();
    void ensureShadowMaps();
    void destroyTargets();
    void computeCascades(const CameraView& camera, const Environment& environment);
    void drawLit(const std::vector<DrawItem>& items, const glm::mat4& toWorld, const std::vector<int>& offsets);
    // cascade: shadow cascade index, or -1 for the camera depth prepass.
    void drawDepth(const std::vector<DrawItem>& items, const glm::mat4& viewProj, int cascade, float time);
    void uploadInstances();
    Shader& litVariant(const DrawItem& item);
    void setLitFrameUniforms(Shader& shader, const CameraView& camera, const glm::mat4& viewProj, const Environment& environment,
                             bool shadowsOn);
    void drawLines(const std::vector<LineVertex>& lines, const glm::mat4& viewProj);
    void renderSsao(const glm::mat4& proj);
    void renderBloom();
    void renderWater(const CameraView& camera, const glm::mat4& viewProj, const Environment& environment, unsigned sceneFbo);
    void renderGodRays(const CameraView& camera, const glm::mat4& viewProj, const Environment& environment);

    RenderSettings m_settings;
    RenderStats m_stats;
    // Timestamp queries: kTimerFrames frames in flight, one query per section boundary.
    static constexpr int kTimerFrames = 4;
    static constexpr int kTimerMarks = static_cast<int>(GpuSection::Count) + 1;
    unsigned m_timerQueries[kTimerFrames][kTimerMarks] = {};
    bool m_timerIssued[kTimerFrames] = {};
    int m_timerFrame = 0;
    void markGpu(int boundary);
    void collectGpuTimes();
    glm::ivec2 m_outputSize{0};
    glm::ivec2 m_sceneSize{0};
    int m_targetSamples = -1;

    // Scene targets.
    unsigned m_msaaFbo = 0;
    unsigned m_msaaColor = 0;
    unsigned m_msaaDepth = 0;
    unsigned m_resolveFbo = 0;
    unsigned m_resolveColor = 0;
    unsigned m_resolveDepth = 0;

    // SSAO (half resolution).
    unsigned m_ssaoFbo = 0;
    unsigned m_ssaoRaw = 0;
    unsigned m_ssaoBlurFbo = 0;
    unsigned m_ssaoBlurred = 0;
    unsigned m_whiteTexture = 0;

    // Bloom chain and god rays.
    std::array<unsigned, kBloomMips> m_bloomTextures{};
    std::array<unsigned, kBloomMips> m_bloomFbos{};
    std::array<glm::ivec2, kBloomMips> m_bloomSizes{};
    unsigned m_copyFbo = 0;
    unsigned m_copyColor = 0;
    unsigned m_copyDepth = 0;
    unsigned m_godRaysFbo = 0;
    unsigned m_godRaysTexture = 0;
    unsigned m_blackTexture = 0;

    // Cascaded shadow maps.
    int m_shadowSize = 0;
    int m_cascadeCount = 0;
    unsigned m_shadowArray = 0;
    std::array<unsigned, kMaxCascades> m_shadowFbos{};
    std::array<glm::mat4, kMaxCascades> m_cascadeViewProj{};
    std::array<float, kMaxCascades> m_cascadeSplits{};
    std::array<float, kMaxCascades> m_cascadeTexel{};
    std::array<float, kMaxCascades> m_cascadeDepthRange{};
    std::array<bool, kMaxCascades> m_cascadeRefresh{};
    std::array<bool, kMaxCascades> m_cascadeValid{}; // Reset when the shadow maps are recreated.
    unsigned m_shadowFrame = 0;
    unsigned m_shadowRawSampler = 0; // Same shadow texture, no depth compare (PCSS blocker search).

    unsigned m_outputFbo = 0;
    unsigned m_emptyVao = 0;
    unsigned m_lineVao = 0;
    unsigned m_lineVbo = 0;
    size_t m_lineCapacity = 0;

    Shader m_litShader;
    Shader m_litInstanced;
    Shader m_litFoliage;
    Shader m_litSkinned;
    Shader m_depthSkinned;
    Shader m_litSkinnedAlpha;   // Hair, eyebrows, eyelashes.
    Shader m_depthSkinnedAlpha;
    Shader m_depthShader;
    Shader m_depthInstanced;
    Shader m_depthFoliage;
    unsigned m_instanceBuffer = 0;
    size_t m_instanceCapacity = 0;
    std::vector<glm::mat4> m_instanceStaging;
    std::vector<int> m_itemInstanceOffsets;
    // Skin palettes: one upload per frame, each distinct palette once (parts of a character share it).
    unsigned m_boneBuffer = 0;
    size_t m_boneCapacity = 0;
    std::vector<glm::mat4> m_boneStaging;
    std::unordered_map<const glm::mat4*, int> m_boneOffsets;
    int boneOffset(const DrawItem& item) const;
    std::vector<int> m_noOffsets;
    float m_time = 0.0f;
    Shader m_skyShader;
    Shader m_compositeShader;
    Shader m_ssaoShader;
    Shader m_ssaoBlurShader;
    Shader m_bloomDownShader;
    Shader m_bloomUpShader;
    Shader m_godRaysShader;
    Shader m_lineShader;
    Shader m_waterShader;
    Shader m_blurShader;
    Shader m_volumetricShader;
    void renderVolumetric(const CameraView& camera, const glm::mat4& viewProj, const Environment& environment);
    unsigned m_dofFbo[2] = {0, 0};
    unsigned m_dofTexture[2] = {0, 0};
    void renderDofBlur();

    Mesh m_cube;
    Mesh m_capsule;
    Mesh m_sphere;

    std::vector<DrawItem> m_items;
    std::vector<DrawItem> m_viewModelItems;
    std::vector<DrawItem> m_transparentItems;
    std::vector<PointLight> m_lights;
    std::vector<LineVertex> m_lines;
    std::vector<LineVertex> m_overlayLines;
};

} // namespace origon
