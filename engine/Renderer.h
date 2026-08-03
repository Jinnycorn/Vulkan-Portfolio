#pragma once

#include "Camera.h"
#include "DescriptorSet.h"
#include "Context.h"
#include "Image2D.h"
#include "StorageBuffer.h"
#include "Sampler.h"
#include "Pipeline.h"
#include "ViewFrustum.h"
#include "Model.h"
#include "MappedBuffer.h"
#include "RenderGraph.h"
#include "ShaderManager.h"
#include "TextureManager.h"
#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include <memory>

namespace hlab {

using namespace std;

struct SceneUniform // Layout matches pbrForward.vert
{
    alignas(16) glm::mat4 projection = glm::mat4(1.0f); // 64 bytes
    alignas(16) glm::mat4 view = glm::mat4(1.0f);       // 64 bytes
    alignas(16) glm::vec3 cameraPos = glm::vec3(0.0f);  // 16 bytes (vec3 + padding)
    alignas(4) float padding1 = 0.0f;                   // 4 bytes padding
    alignas(16) glm::vec3 directionalLightDir = glm::vec3(0.0f, 1.0f, 0.0f); // 16 bytes
    alignas(16) glm::vec3 directionalLightColor = glm::vec3(1.0f);
    alignas(16) glm::mat4 lightSpaceMatrix = glm::mat4(1.0f); // 64 bytes - for shadow mapping
    alignas(16) glm::mat4 projectionNoJitter = glm::mat4(1.0f);
    alignas(16) glm::mat4 previousProjectionNoJitter = glm::mat4(1.0f);
    alignas(16) glm::mat4 previousView = glm::mat4(1.0f);
    // xy=current jitter (pixels), zw=previous jitter (pixels)
    alignas(16) glm::vec4 temporalJitter = glm::vec4(0.0f);
    // x=history reset, y=frame index, z=render width, w=render height
    alignas(16) glm::vec4 temporalParams = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
    // x=current exposure, y=previous exposure
    alignas(16) glm::vec4 temporalExposure = glm::vec4(1.0f);
};

struct SkyOptionsUBO
{
    float environmentIntensity = 1.0f;
    float roughnessLevel = 0.5f;
    uint32_t useIrradianceMap = 0;

    uint32_t showMipLevels = 0;
    uint32_t showCubeFaces = 0;
    float padding1;
    float padding2;
    float padding3;
};

struct OptionsUniform
{
    alignas(4) int textureOn = 1;        // Use int instead of bool, 1 = true, 0 = false
    alignas(4) int shadowOn = 1;         // Use int instead of bool, 1 = true, 0 = false
    alignas(4) int discardOn = 1;        // Use int instead of bool, 1 = true, 0 = false
    alignas(4) int animationOn = 1;      // Use int instead of bool, 1 = true, 0 = false
    alignas(4) float specularWeight = 0.05f;  // PBR specular weight - reduced default
    alignas(4) float diffuseWeight = 1.0f;    // PBR diffuse weight
    alignas(4) float emissiveWeight = 1.0f;   // PBR emissive weight
    alignas(4) float shadowOffset = 0.0f;     // Shadow bias offset
};

// Post-processing options uniform buffer structure
struct PostOptionsUBO
{
    // Basic tone mapping
    alignas(4) int toneMappingType = 2; // 0=None, 1=Reinhard, 2=ACES, etc.
    alignas(4) float exposure = 1.0f;   // Exposure adjustment
    alignas(4) float gamma = 2.2f;      // Gamma correction
    alignas(4) float maxWhite = 11.2f;  // For Reinhard extended

    // Color grading
    alignas(4) float contrast = 1.0f;   // Contrast adjustment
    alignas(4) float brightness = 0.0f; // Brightness adjustment
    alignas(4) float saturation = 1.0f; // Saturation adjustment
    alignas(4) float vibrance = 0.0f;   // Vibrance adjustment

    // Effects
    alignas(4) float vignetteStrength = 0.0f;    // Vignette effect strength
    alignas(4) float vignetteRadius = 0.8f;      // Vignette radius
    alignas(4) float filmGrainStrength = 0.0f;   // Film grain effect
    alignas(4) float chromaticAberration = 1.65f; // FXAA enabled by default (0.65 strength)

    // Debug options
    alignas(4) int debugMode = 0;       // Debug visualization mode
    alignas(4) int showOnlyChannel = 0; // Show specific color channel
    alignas(4) float debugSplit = 0.5f; // Split screen position for comparison
    alignas(4) float padding1 = 0.0f;   // Bokeh parameter packing

    // Low-VRAM NVIDIA Image Scaling style spatial upscaler.
    // It runs in the existing post pass, so it does not allocate another full-resolution image.
    // 0 = AMD FSR 2.2 temporal path (default), 1 = NVIDIA NIS spatial path.
    alignas(4) int nisEnabled = 0;
    alignas(4) float nisSharpness = 0.35f;
    alignas(4) float nisScaleThreshold = 1.01f;
    alignas(4) float nisPadding = 0.0f;
};

static_assert(sizeof(PostOptionsUBO) == 80, "PostOptionsUBO must match post.frag std140 layout");
static_assert(sizeof(PostOptionsUBO) % 16 == 0, "PostOptionsUBO must be 16-byte aligned in size");

struct SsaoOptionsUBO
{
    alignas(4) float ssaoRadius = 0.1f;
    alignas(4) float ssaoBias = 0.025f;
    alignas(4) int ssaoSampleCount = 16;
    alignas(4) float ssaoPower = 2.0f;
};

struct BoneDataUniform
{
    alignas(16) glm::mat4 boneMatrices[65]; // 4,160 bytes (already 16-byte aligned)
    alignas(16) glm::mat4 previousBoneMatrices[65];
    alignas(16) glm::vec4 animationData;    // x = hasAnimation (0.0/1.0), y,z,w = future use
};

static_assert(sizeof(BoneDataUniform) % 16 == 0, "BoneDataUniform must be 16-byte aligned");
static_assert(sizeof(BoneDataUniform) == 2 * 65 * 64 + 16, "Unexpected BoneDataUniform size");

// Push constants structure for PBR forward rendering
struct PbrPushConstants
{
    alignas(16) glm::mat4 model = glm::mat4(1.0f); // 64 bytes
    alignas(4) uint32_t materialIndex = 0;         // 4 bytes - Material index for bindless access
    alignas(4) float coeffs[15] = {
        0.0f}; // 60 bytes (reduced from 16 to make room for materialIndex)
};

static_assert(sizeof(PbrPushConstants) == 128, "PbrPushConstants must be 128 bytes");

struct CullingStats
{
    uint32_t totalMeshes = 0;
    uint32_t culledMeshes = 0;
    uint32_t occlusionCulledMeshes = 0;
    uint32_t lod1Meshes = 0;
    uint32_t lod2Meshes = 0;
    uint32_t lodCulledMeshes = 0;
    uint32_t renderedMeshes = 0;
};

class Renderer
{
  public:
    Renderer(Context& ctx, ShaderManager& shaderManager, const uint32_t& kMaxFramesInFlight,
             const string& kAssetsPathPrefix, const string& kShaderPathPrefix_,
             vector<unique_ptr<Model>>& models, VkFormat outColorFormat, VkFormat depthFormat,
             uint32_t swapChainWidth, uint32_t swapChainHeight);

    ~Renderer();

    void createPipelines(const VkFormat colorFormat, const VkFormat depthFormat);
    void createTextures(uint32_t swapchainWidth, uint32_t swapchainHeight);
    void resize(uint32_t swapchainWidth, uint32_t swapchainHeight);
    void createUniformBuffers();
    void update(Camera& camera, vector<unique_ptr<Model>>& models, uint32_t currentFrame,
                double time);
    void updateBoneData(const vector<unique_ptr<Model>>& models, uint32_t currentFrame);
    void draw(VkCommandBuffer cmd, uint32_t currentFrame, VkImageView swapchainImageView,
              vector<unique_ptr<Model>>& models, VkViewport viewport, VkRect2D scissor);
    void invalidateTemporalHistory();
    void setUpscalerMode(int mode);
    int upscalerMode() const { return postOptionsUBO_.nisEnabled; }

    // View frustum culling
    auto getCullingStats() const -> const CullingStats&;
    bool isFrustumCullingEnabled() const;
    void performFrustumCulling(vector<unique_ptr<Model>>& models);
    void updateWorldBounds(vector<unique_ptr<Model>>& models);
    void setFrustumCullingEnabled(bool enabled);
    void updateViewFrustum(const glm::mat4& viewProjection);

    // Temporal GPU occlusion culling
    bool isOcclusionCullingEnabled() const;
    void setOcclusionCullingEnabled(bool enabled);

    // Screen-space automatic LOD. Thresholds are projected mesh radii in pixels.
    bool isLodEnabled() const { return lodEnabled_; }
    void setLodEnabled(bool enabled) { lodEnabled_ = enabled; }
    float lod1PixelThreshold() const { return lod1PixelThreshold_; }
    float lod2PixelThreshold() const { return lod2PixelThreshold_; }
    float lodCullPixelThreshold() const { return lodCullPixelThreshold_; }
    void setLodThresholds(float lod1Pixels, float lod2Pixels, float cullPixels)
    {
        lod1PixelThreshold_ = lod1Pixels;
        lod2PixelThreshold_ = lod2Pixels;
        lodCullPixelThreshold_ = cullPixels;
    }

    auto sceneUBO() -> SceneUniform&
    {
        return sceneUBO_;
    }
    auto optionsUBO() -> OptionsUniform&
    {
        return optionsUBO_;
    }
    auto skyOptionsUBO() -> SkyOptionsUBO&
    {
        return skyOptionsUBO_;
    }
    auto postOptionsUBO() -> PostOptionsUBO&
    {
        return postOptionsUBO_;
    }
    auto ssaoOptionsUBO() -> SsaoOptionsUBO&
    {
        return ssaoOptionsUBO_;
    }

  private:
    const uint32_t& kMaxFramesInFlight_; // 2;
    const string& kAssetsPathPrefix_;    // "../../assets/";
    const string& kShaderPathPrefix_;    // kAssetsPathPrefix + "shaders/";

    Context& ctx_;
    ShaderManager& shaderManager_;

    // Per frame uniform buffers
    SceneUniform sceneUBO_{};
    SkyOptionsUBO skyOptionsUBO_{};
    OptionsUniform optionsUBO_{};
    BoneDataUniform boneDataUBO_{};
    PostOptionsUBO postOptionsUBO_{};
    SsaoOptionsUBO ssaoOptionsUBO_{};

    // Resources - Consolidated uniform buffers using map structure
    unordered_map<string, vector<unique_ptr<MappedBuffer>>> perFrameUniformBuffers_;
    // Keys: "sceneData", "skyOptions", "options", "boneData", "postOptions", "ssaoOptions"

    // Resources - Consolidated image buffers using map structure
    unordered_map<string, unique_ptr<Image2D>> imageBuffers_;
    // Keys: "depthStencil", "floatColor1", "floatColor2", "shadowMap", "prefilteredMap",
    // "irradianceMap", "brdfLut", "gAlbedo", "gNormal", "gPosition", "gMaterial"

    unique_ptr<TextureManager> materialTextures_; // Material textures for bindless rendering
    unique_ptr<StorageBuffer> materialBuffer_;    // Material data storage buffer

    Sampler samplerLinearRepeat_;
    Sampler samplerLinearClamp_;
    Sampler samplerAnisoRepeat_;
    Sampler samplerAnisoClamp_;
    Sampler samplerShadow_;

    unordered_map<string, DescriptorSet> descriptorSets_;
    // Keys: "material", "sky", "post", "shadowMap"

    unordered_map<string, vector<DescriptorSet>> perFrameDescriptorSets_;
    // Keys: "sceneOptions", "skyOptions", "postProcessing", "ssao"

    unordered_map<string, unique_ptr<Pipeline>> pipelines_;

    bool perFrameResources(vector<string> resourceNames)
    {
        for (const auto& name : resourceNames) {
            if (perFrameUniformBuffers_.find(name) != perFrameUniformBuffers_.end()) {
                return true;
            }
        }

        return false;
    }

    void addResource(string resourceName, uint32_t frameNumber,
                     vector<reference_wrapper<Resource>>& resources)
    {
        // Temporal history is ping-ponged by frame-in-flight. Keeping these aliases here lets
        // shader reflection retain stable FSR-style names while each descriptor set receives
        // the correct previous/current surface.
        if (resourceName == "historyColorPrev") {
            resources.push_back(*imageBuffers_[frameNumber == 0 ? "historyColor1" : "historyColor0"]);
            return;
        }
        if (resourceName == "historyColorCurrent") {
            resources.push_back(*imageBuffers_[frameNumber == 0 ? "historyColor0" : "historyColor1"]);
            return;
        }
        if (resourceName == "historyDepthPrev") {
            resources.push_back(*imageBuffers_[frameNumber == 0 ? "historyDepth1" : "historyDepth0"]);
            return;
        }
        if (resourceName == "historyDepthCurrent") {
            resources.push_back(*imageBuffers_[frameNumber == 0 ? "historyDepth0" : "historyDepth1"]);
            return;
        }
        if (perFrameUniformBuffers_.find(resourceName) != perFrameUniformBuffers_.end()) {
            resources.push_back(*perFrameUniformBuffers_[resourceName][frameNumber]);
            return;
        }

        if (imageBuffers_.find(resourceName) != imageBuffers_.end()) {
            resources.push_back(*imageBuffers_[resourceName]);
            return;
        }

        if (resourceName == "materialBuffer") {
            resources.push_back(*materialBuffer_);
            return;
        }

        if (resourceName == "materialTextures") {
            resources.push_back(*materialTextures_);
            return;
        }
    }

    RenderGraph renderGraph_;

    ViewFrustum viewFrustum_{};
    bool frustumCullingEnabled_{true};
    vector<glm::mat4> cachedModelMatrices_{};
    vector<uint8_t> worldBoundsValid_{};

    VkQueryPool occlusionQueryPool_{VK_NULL_HANDLE};
    uint32_t meshQueryCount_{0};
    vector<uint8_t> occlusionVisible_{};
    vector<uint8_t> occlusionMissCounts_{};
    vector<vector<uint8_t>> occlusionQueryIssued_{};
    vector<uint64_t> occlusionQueryResults_{};
    bool occlusionCullingEnabled_{true};
    uint64_t renderFrameCounter_{0};
    static constexpr uint32_t kOcclusionRetestInterval = 8;

    bool lodEnabled_{false};
    float lod1PixelThreshold_{110.0f};
    float lod2PixelThreshold_{36.0f};
    float lodCullPixelThreshold_{2.0f};

    bool temporalHistoryValid_{false};
    bool temporalModeChanged_{false};
    glm::mat4 previousView_{1.0f};
    glm::mat4 previousProjectionNoJitter_{1.0f};
    glm::vec2 previousJitterPixels_{0.0f};
    float previousExposure_{1.0f};
    uint32_t temporalRenderWidth_{1};
    uint32_t temporalRenderHeight_{1};
    unordered_map<const Mesh*, glm::mat4> previousMeshTransforms_{};

    void createOcclusionResources(const vector<unique_ptr<Model>>& models);
    void resolveOcclusionQueries(uint32_t currentFrame);

    // Statistics
    CullingStats cullingStats_;

    // HDR format optimization
    VkFormat selectedHDRFormat_{VK_FORMAT_R16G16B16A16_SFLOAT}; // Store selected HDR format

    // Format selection functions with proper priority
    VkFormat selectOptimalHDRFormat(bool needsAlpha, bool fullPrecision);

    // Format validation and creation
    bool isFormatSuitableForHDR(VkFormat format);

    // Utility functions
    void logHDRMemoryUsage(uint32_t width, uint32_t height);

    // Helper functions for creating rendering structures
    VkRenderingAttachmentInfo
    createColorAttachment(VkImageView imageView,
                          VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                          VkClearColorValue clearColor = {0.0f, 0.0f, 0.0f, 0.0f},
                          VkImageView resolveImageView = VK_NULL_HANDLE,
                          VkResolveModeFlagBits resolveMode = VK_RESOLVE_MODE_NONE) const;

    VkRenderingAttachmentInfo
    createDepthAttachment(VkImageView imageView,
                          VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                          float clearDepth = 1.0f, VkImageView resolveImageView = VK_NULL_HANDLE,
                          VkResolveModeFlagBits resolveMode = VK_RESOLVE_MODE_NONE) const;
};

} // namespace hlab
