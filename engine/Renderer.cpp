#include "Renderer.h"
#include "Logger.h"
#include "TracyProfiler.h" // Add Tracy macros wrapper
#include <stb_image.h>
#include <cstdlib>
#include <algorithm>
#include <cmath>

namespace hlab {

Renderer::~Renderer()
{
    if (occlusionQueryPool_ != VK_NULL_HANDLE) {
        vkDestroyQueryPool(ctx_.device(), occlusionQueryPool_, nullptr);
        occlusionQueryPool_ = VK_NULL_HANDLE;
    }
}

void Renderer::createOcclusionResources(const vector<unique_ptr<Model>>& models)
{
    meshQueryCount_ = 0;
    for (const auto& model : models) {
        meshQueryCount_ += static_cast<uint32_t>(model->meshes().size());
    }

    if (meshQueryCount_ == 0) {
        return;
    }

    VkQueryPoolCreateInfo queryPoolInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    queryPoolInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
    queryPoolInfo.queryCount = meshQueryCount_ * kMaxFramesInFlight_;
    check(vkCreateQueryPool(ctx_.device(), &queryPoolInfo, nullptr, &occlusionQueryPool_));

    occlusionVisible_.assign(meshQueryCount_, 1);
    occlusionMissCounts_.assign(meshQueryCount_, 0);
    occlusionQueryIssued_.assign(kMaxFramesInFlight_, vector<uint8_t>(meshQueryCount_, 0));
    // Each query returns {passed sample count, availability}.
    occlusionQueryResults_.assign(static_cast<size_t>(meshQueryCount_) * 2, 0);

    printLog("GPU occlusion culling initialized for {} meshes", meshQueryCount_);
}

void Renderer::resolveOcclusionQueries(uint32_t currentFrame)
{
    if (!occlusionCullingEnabled_ || occlusionQueryPool_ == VK_NULL_HANDLE ||
        currentFrame >= occlusionQueryIssued_.size()) {
        return;
    }

    const uint32_t queryBase = currentFrame * meshQueryCount_;
    std::fill(occlusionQueryResults_.begin(), occlusionQueryResults_.end(), uint64_t{0});

    // Fetch the entire frame range in one driver call. Availability keeps untouched queries safe.
    VkResult result = vkGetQueryPoolResults(
        ctx_.device(), occlusionQueryPool_, queryBase, meshQueryCount_,
        occlusionQueryResults_.size() * sizeof(uint64_t), occlusionQueryResults_.data(),
        2 * sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

    if (result != VK_SUCCESS && result != VK_NOT_READY) {
        return;
    }

    for (uint32_t i = 0; i < meshQueryCount_; ++i) {
        if (!occlusionQueryIssued_[currentFrame][i]) {
            continue;
        }

        const uint64_t passedSamples = occlusionQueryResults_[static_cast<size_t>(i) * 2];
        const uint64_t available = occlusionQueryResults_[static_cast<size_t>(i) * 2 + 1];
        if (available == 0) {
            continue;
        }

        if (passedSamples > 0) {
            occlusionVisible_[i] = 1;
            occlusionMissCounts_[i] = 0;
        } else {
            // Require two completed zero-sample results to avoid one-frame false positives.
            occlusionMissCounts_[i] =
                static_cast<uint8_t>(std::min<uint32_t>(occlusionMissCounts_[i] + 1, 2));
            if (occlusionMissCounts_[i] >= 2) {
                occlusionVisible_[i] = 0;
            }
        }
    }
}

Renderer::Renderer(Context& ctx, ShaderManager& shaderManager, const uint32_t& kMaxFramesInFlight,
                   const string& kAssetsPathPrefix, const string& kShaderPathPrefix_,
                   vector<unique_ptr<Model>>& models, VkFormat outColorFormat, VkFormat depthFormat,
                   uint32_t swapChainWidth, uint32_t swapChainHeight)
    : ctx_(ctx), shaderManager_(shaderManager), kMaxFramesInFlight_(kMaxFramesInFlight),
      kAssetsPathPrefix_(kAssetsPathPrefix), kShaderPathPrefix_(kShaderPathPrefix_),
      samplerShadow_(ctx), samplerLinearRepeat_(ctx), samplerLinearClamp_(ctx),
      samplerAnisoRepeat_(ctx), samplerAnisoClamp_(ctx),
      materialTextures_(std::make_unique<TextureManager>(ctx))
{
    TRACY_CPU_SCOPE("Renderer::Constructor");

    createOcclusionResources(models);

    const char* lowSpecValue = std::getenv("HLAB_LOW_SPEC");
    const bool lowSpecMode = lowSpecValue != nullptr && string(lowSpecValue) != "0";
    if (lowSpecMode) {
        // Start safely on 2 GB GPUs. The Quality tab can enable features incrementally.
        optionsUBO_.shadowOn = 0;
        ssaoOptionsUBO_.ssaoSampleCount = 4;
        ssaoOptionsUBO_.ssaoRadius = 0.075f;
        postOptionsUBO_.toneMappingType = 1;
        postOptionsUBO_.chromaticAberration = 1.55f;
        postOptionsUBO_.vignetteStrength = 0.0f;
        postOptionsUBO_.filmGrainStrength = 0.0f;
        lod1PixelThreshold_ = 115.0f;
        lod2PixelThreshold_ = 38.0f;
        lodCullPixelThreshold_ = 2.0f;
        printLog("Low-spec renderer preset enabled (Optimized quality level + automatic LOD)");
    }

    {
        TRACY_CPU_SCOPE("Create Pipelines");
        createPipelines(outColorFormat, depthFormat);
    }

    {
        TRACY_CPU_SCOPE("Create Textures");
        createTextures(swapChainWidth, swapChainHeight);
    }

    {
        TRACY_CPU_SCOPE("Create Uniform Buffers");
        createUniformBuffers();
    }

    {
        TRACY_CPU_SCOPE("Setup Material Buffers");
        vector<MaterialUBO> allMaterials;

        for (auto& m : models) {
            m->prepareForBindlessRendering(samplerLinearRepeat_, allMaterials, *materialTextures_);
        }

        materialBuffer_ = std::make_unique<StorageBuffer>(ctx_, allMaterials.data(),
                                                     sizeof(MaterialUBO) * allMaterials.size());
    }

    {
        TRACY_CPU_SCOPE("Setup Descriptor Sets");
        // ... existing descriptor set creation code ...
        unordered_map<string, vector<string>> descriptorSetNames; // TODO: move to script
        descriptorSetNames["shadowMap"] = {"sceneOptions"};
        descriptorSetNames["pbrDeferred"] = {"sceneOptions", "material"};
        descriptorSetNames["sky"] = {"skyOptions", "sky"};
        descriptorSetNames["deferredLighting"] = {"deferredLightingData"};
        descriptorSetNames["post"] = {"postProcessing"};

        unordered_map<string, vector<vector<BindingInfo>>> bindingInfos =
            shaderManager_.bindingInfos();

        for (auto i : descriptorSetNames) {
            auto pipelineName = i.first;

            auto& bindings = bindingInfos.at(pipelineName);

            assert(bindings.size() == descriptorSetNames[pipelineName].size());

            for (int s = 0; s < bindings.size(); s++) {

                string setName = descriptorSetNames[pipelineName][s];

                if (perFrameDescriptorSets_.find(setName) != perFrameDescriptorSets_.end())
                    continue;
                if (descriptorSets_.find(setName) != descriptorSets_.end())
                    continue;

                vector<string> bindingNames;
                for (int b = 0; b < bindings[s].size(); b++) {
                    bindingNames.push_back(bindings[s][b].resourceName);
                }

                bool perFramesSet = this->perFrameResources(bindingNames);

                if (perFramesSet) {
                    perFrameDescriptorSets_[setName].resize(kMaxFramesInFlight_);
                    for (uint32_t i = 0; i < kMaxFramesInFlight_; i++) {
                        // Collect resources for this descriptor set
                        vector<reference_wrapper<Resource>> resources;

                        for (const string& resourceName : bindingNames) {
                            addResource(resourceName, i, resources);
                        }

                        // Create the descriptor set with collected resources
                        perFrameDescriptorSets_[setName][i].create(
                            ctx_, pipelines_[pipelineName]->layouts()[s], resources);
                    }
                } else {
                    // Collect resources for non-per-frame descriptor set
                    vector<reference_wrapper<Resource>> resources;

                    for (const string& resourceName : bindingNames) {
                        addResource(resourceName, uint32_t(-1), resources);
                    }

                    // Create the descriptor set with collected resources
                    descriptorSets_[setName].create(ctx_, pipelines_[pipelineName]->layouts()[s],
                                                    resources);
                }
            }

            // Update pipeline's descriptor sets with the created descriptor sets for this pipeline
            vector<vector<reference_wrapper<DescriptorSet>>> pipelineDescriptorSets;
            pipelineDescriptorSets.resize(kMaxFramesInFlight_);

            for (uint32_t frameIndex = 0; frameIndex < kMaxFramesInFlight_; ++frameIndex) {
                pipelineDescriptorSets[frameIndex].reserve(descriptorSetNames[pipelineName].size());

                for (size_t setIndex = 0; setIndex < descriptorSetNames[pipelineName].size();
                     ++setIndex) {
                    const string& setName = descriptorSetNames[pipelineName][setIndex];

                    // Check if this is a per-frame descriptor set
                    if (perFrameDescriptorSets_.find(setName) != perFrameDescriptorSets_.end()) {
                        // For per-frame sets, use the specific frame
                        pipelineDescriptorSets[frameIndex].emplace_back(
                            std::ref(perFrameDescriptorSets_[setName][frameIndex]));
                    } else if (descriptorSets_.find(setName) != descriptorSets_.end()) {
                        // For non-per-frame sets, use the same descriptor set for all frames
                        pipelineDescriptorSets[frameIndex].emplace_back(
                            std::ref(descriptorSets_[setName]));
                    }
                }
            }

            // Set the descriptor sets on the pipeline
            pipelines_[pipelineName]->setDescriptorSets(pipelineDescriptorSets);
        }
    }
}

void Renderer::createUniformBuffers()
{
    TRACY_CPU_SCOPE("Renderer::createUniformBuffers");

    const VkDevice device = ctx_.device();

    // Initialize uniform buffer map with proper keys
    const vector<string> bufferNames = {"sceneData", "skyOptions",  "options",
                                        "boneData",  "postOptions", "ssaoOptions"};

    for (const auto& name : bufferNames) {
        perFrameUniformBuffers_[name].clear();
        perFrameUniformBuffers_[name].reserve(kMaxFramesInFlight_);
    }

    // Create uniform buffers for each type and frame
    for (uint32_t i = 0; i < kMaxFramesInFlight_; ++i) {
        // Scene uniform buffers
        auto sceneBuffer = std::make_unique<MappedBuffer>(ctx_);
        sceneBuffer->createUniformBuffer(sceneUBO_);
        perFrameUniformBuffers_["sceneData"].emplace_back(std::move(sceneBuffer));

        // Options uniform buffers
        auto optionsBuffer = std::make_unique<MappedBuffer>(ctx_);
        optionsBuffer->createUniformBuffer(optionsUBO_);
        perFrameUniformBuffers_["options"].emplace_back(std::move(optionsBuffer));

        // Sky options uniform buffers
        auto skyOptionsBuffer = std::make_unique<MappedBuffer>(ctx_);
        skyOptionsBuffer->createUniformBuffer(skyOptionsUBO_);
        perFrameUniformBuffers_["skyOptions"].emplace_back(std::move(skyOptionsBuffer));

        // Post options uniform buffers
        auto postOptionsBuffer = std::make_unique<MappedBuffer>(ctx_);
        postOptionsBuffer->createUniformBuffer(postOptionsUBO_);
        perFrameUniformBuffers_["postOptions"].emplace_back(std::move(postOptionsBuffer));

        // SSAO options uniform buffers
        auto ssaoOptionsBuffer = std::make_unique<MappedBuffer>(ctx_);
        ssaoOptionsBuffer->createUniformBuffer(ssaoOptionsUBO_);
        perFrameUniformBuffers_["ssaoOptions"].emplace_back(std::move(ssaoOptionsBuffer));

        // Bone data uniform buffers
        auto boneDataBuffer = std::make_unique<MappedBuffer>(ctx_);
        boneDataBuffer->createUniformBuffer(boneDataUBO_);
        perFrameUniformBuffers_["boneData"].emplace_back(std::move(boneDataBuffer));
    }
}

void Renderer::update(Camera& camera, vector<unique_ptr<Model>>& models, uint32_t currentFrame,
                      double time)
{
    TRACY_CPU_SCOPE("Renderer::update");

    {
        TRACY_CPU_SCOPE("Resolve Occlusion Queries");
        resolveOcclusionQueries(currentFrame);
    }

    {
        TRACY_CPU_SCOPE("Update View Frustum");
        // Update view frustum based on current camera view-projection matrix
        updateViewFrustum(camera.matrices.perspective * camera.matrices.view);
    }

    {
        TRACY_CPU_SCOPE("Update World Bounds");
        updateWorldBounds(models);
    }

    {
        TRACY_CPU_SCOPE("Update Bone Data");
        updateBoneData(models, currentFrame);
    }

    {
        TRACY_CPU_SCOPE("Perform Frustum Culling");
        performFrustumCulling(models);
    }

    {
        TRACY_CPU_SCOPE("Update Uniform Buffers");
        // Update all uniform buffers using direct iteration over the map
        for (const auto& [bufferName, bufferVector] : perFrameUniformBuffers_) {
            bufferVector[currentFrame]->updateFromCpuData();
        }
    }
}

void Renderer::updateBoneData(const vector<unique_ptr<Model>>& models, uint32_t currentFrame)
{
    TRACY_CPU_SCOPE("Renderer::updateBoneData");

    // Reset bone data
    boneDataUBO_.animationData.x = 0.0f;
    for (int i = 0; i < 65; ++i) {
        boneDataUBO_.boneMatrices[i] = glm::mat4(1.0f);
    }

    // Check if any model has animation data
    bool hasAnyAnimation = false;
    for (const auto& model : models) {
        if (model->hasAnimations() && model->hasBones()) {
            hasAnyAnimation = true;

            // Get bone matrices from the first animated model
            const auto& boneMatrices = model->getBoneMatrices();

            // Copy bone matrices (up to 65 bones)
            const size_t maxBones = 65;
            size_t bonesToCopy = (boneMatrices.size() < maxBones) ? boneMatrices.size() : maxBones;
            for (size_t i = 0; i < bonesToCopy; ++i) {
                boneDataUBO_.boneMatrices[i] = boneMatrices[i];
            }

            break; // For now, use the first animated model
        }
    }

    boneDataUBO_.animationData.x = float(hasAnyAnimation);

    // DEBUG: Log hasAnimation state
    static bool lastHasAnimation = false;
    if (lastHasAnimation != hasAnyAnimation) {
        printLog("hasAnimation changed to: {}", hasAnyAnimation);
        lastHasAnimation = hasAnyAnimation;
    }

    // Update the GPU buffer using the consolidated map structure
    perFrameUniformBuffers_["boneData"][currentFrame]->updateFromCpuData();
}

void Renderer::draw(VkCommandBuffer cmd, uint32_t currentFrame, VkImageView swapchainImageView,
                    vector<unique_ptr<Model>>& models, VkViewport viewport, VkRect2D scissor)
{
    TRACY_CPU_SCOPE("Renderer::draw");

    const float lodReferenceHeight = std::max(1.0f, viewport.height);

    const bool periodicOcclusionRetest =
        (renderFrameCounter_ % kOcclusionRetestInterval) == 0;
    cullingStats_.occlusionCulledMeshes = 0;
    cullingStats_.lod1Meshes = 0;
    cullingStats_.lod2Meshes = 0;
    cullingStats_.lodCulledMeshes = 0;

    if (occlusionCullingEnabled_ && occlusionQueryPool_ != VK_NULL_HANDLE) {
        const uint32_t queryBase = currentFrame * meshQueryCount_;
        vkCmdResetQueryPool(cmd, occlusionQueryPool_, queryBase, meshQueryCount_);
        std::fill(occlusionQueryIssued_[currentFrame].begin(),
                  occlusionQueryIssued_[currentFrame].end(), uint8_t{0});
    }

    for (auto& renderNode : renderGraph_.renderNodes_) {

        if (renderNode.pipelineNames[0] == "shadowMap" && optionsUBO_.shadowOn == 0) {
            continue;
        }

        if (renderNode.pipelineNames[0] == "deferredLighting") {
            TRACY_CPU_SCOPE("deferredLighting");
            pipelines_.at("deferredLighting")->dispatch(cmd, currentFrame); // Compute
            continue;
        }

        string mainTarget;
        vector<VkRenderingAttachmentInfo> colorAttachments{};
        VkRenderingAttachmentInfo depthAttachment{};
        VkRenderingAttachmentInfo stencilAttachment{};
        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;

        // Direct rendering (no MSAA) - simplified path

        // Handle color attachments
        {
            TRACY_CPU_SCOPE("Setup Color Attachments");
            for (const auto& colorTarget : renderNode.colorAttachments) {
                if (colorTarget == "swapchain") {
                    colorAttachments.push_back(createColorAttachment(
                        swapchainImageView, VK_ATTACHMENT_LOAD_OP_CLEAR, {0.0f, 0.0f, 1.0f, 0.0f}));
                } else {
                    if (mainTarget.empty()) {
                        mainTarget = colorTarget;
                    }

                    // Transition color attachment to correct layout (keeps barrierHelper in sync)
                    imageBuffers_[colorTarget]->transitionToColorAttachment(cmd);

                    if (renderNode.pipelineNames[0] == "sky") {
                        colorAttachments.push_back(createColorAttachment(
                            imageBuffers_[colorTarget]->view(), VK_ATTACHMENT_LOAD_OP_LOAD,
                            {0.0f, 0.0f, 0.5f, 0.0f}));
                    } else {
                        colorAttachments.push_back(createColorAttachment(
                            imageBuffers_[colorTarget]->view(), VK_ATTACHMENT_LOAD_OP_CLEAR,
                            {0.0f, 0.0f, 0.5f, 0.0f}));
                    }
                }
            }
        }

        // Handle depth attachment
        {
            TRACY_CPU_SCOPE("Setup Depth Attachment");
            if (!renderNode.depthAttachment.empty()) {
                if (mainTarget.empty()) {
                    mainTarget = renderNode.depthAttachment;
                }
                imageBuffers_[renderNode.depthAttachment]->transitionToDepthStencilAttachment(cmd);

                if (renderNode.pipelineNames[0] == "sky") {
                    depthAttachment = createDepthAttachment(
                        imageBuffers_[renderNode.depthAttachment]->attachmentView(),
                        VK_ATTACHMENT_LOAD_OP_LOAD, 1.0f);
                } else {
                    depthAttachment = createDepthAttachment(
                        imageBuffers_[renderNode.depthAttachment]->attachmentView(),
                        VK_ATTACHMENT_LOAD_OP_CLEAR, 1.0f);
                }

                renderingInfo.pDepthAttachment = &depthAttachment;
            }
        }

        {
            TRACY_CPU_SCOPE("Submit Pipeline Barriers");
            for (auto& pipelineName : renderNode.pipelineNames) {
                pipelines_.at(pipelineName)->submitBarriers(cmd, currentFrame);
            }
        }

        uint32_t width = uint32_t(viewport.width);
        uint32_t height = uint32_t(viewport.height);
        if (!mainTarget.empty()) {
            width = imageBuffers_[mainTarget]->width();
            height = imageBuffers_[mainTarget]->height();
        }

        VkRect2D renderArea = {0, 0, width, height};
        renderingInfo.renderArea = renderArea;
        renderingInfo.layerCount = 1;
        if (colorAttachments.size() > 0) {
            renderingInfo.colorAttachmentCount = uint32_t(colorAttachments.size());
            renderingInfo.pColorAttachments = colorAttachments.data();
        }

        VkViewport viewport{0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f};
        VkRect2D scissor{0, 0, width, height};

        {
            TRACY_CPU_SCOPE("Begin Rendering");
            vkCmdBeginRendering(cmd, &renderingInfo);
            vkCmdSetViewport(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissor);
        }

        // Process all pipelines for this render node
        {
            TRACY_CPU_SCOPE("ProcessPipelines");

            for (auto& pipelineName : renderNode.pipelineNames) {
                // Use a scoped block for each pipeline instead of dynamic scope
                {
                    TRACY_CPU_SCOPE("Pipeline Processing");
                    
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      pipelines_.at(pipelineName)->pipeline());

                    pipelines_.at(pipelineName)->bindDescriptorSets(cmd, currentFrame);

                    if (pipelineName == "sky") {
                        TRACY_CPU_SCOPE("drawSky");
                        vkCmdDraw(cmd, 36, 1, 0, 0);
                        continue;
                    }

                    if (pipelineName == "post") {
                        TRACY_CPU_SCOPE("drawPost");
                        vkCmdDraw(cmd, 6, 1, 0, 0);
                        continue;
                    }

                    if (pipelineName == "shadowMap") {
                        TRACY_CPU_SCOPE("shadowMapSetup");
                        vkCmdSetDepthBias(cmd,
                                          1.1f,  // Constant factor
                                          0.0f,  // Clamp value
                                          2.0f); // Slope factor
                    }

                    // Render all visible models for this pipeline
                    {
                        TRACY_CPU_SCOPE("DrawModels");

                        VkDeviceSize offsets[1]{0};
                        size_t totalMeshCount = 0;
                        size_t globalMeshIndex = 0;

                        struct DrawItem
                        {
                            Model* model;
                            Mesh* mesh;
                            size_t queryMeshIndex;
                            float distanceSquared;
                            uint32_t lodLevel;
                        };

                        vector<DrawItem> drawItems;
                        drawItems.reserve(meshQueryCount_);

                        for (size_t j = 0; j < models.size(); j++) {
                            // Keep the global index stable even when a model is hidden so query
                            // results always map to the same mesh.
                            for (size_t i = 0; i < models[j]->meshes().size(); i++) {
                                auto& mesh = models[j]->meshes()[i];
                                const size_t queryMeshIndex = globalMeshIndex++;
                                totalMeshCount++;

                                if (!models[j]->visible() || !mesh.editorVisible ||
                                    mesh.isCulled) {
                                    continue;
                                }

                                const bool occlusionPass = pipelineName == "pbrDeferred";
                                const bool isTemporallyOccluded =
                                    occlusionPass && occlusionCullingEnabled_ &&
                                    !periodicOcclusionRetest &&
                                    queryMeshIndex < occlusionVisible_.size() &&
                                    occlusionVisible_[queryMeshIndex] == 0;
                                if (isTemporallyOccluded) {
                                    ++cullingStats_.occlusionCulledMeshes;
                                    continue;
                                }

                                const glm::vec3 toMesh =
                                    mesh.worldBounds.getCenter() - sceneUBO_.cameraPos;
                                const float distanceSquared = glm::dot(toMesh, toMesh);
                                uint32_t lodLevel = 0;

                                if (lodEnabled_ && !models[j]->hasBones()) {
                                    const float distance =
                                        std::sqrt(std::max(distanceSquared, 0.0001f));
                                    const float worldRadius =
                                        glm::length(mesh.worldBounds.getExtents());
                                    const float projectedRadiusPixels =
                                        worldRadius * lodReferenceHeight / distance;

                                    if (projectedRadiusPixels < lodCullPixelThreshold_) {
                                        if (pipelineName == "pbrDeferred") {
                                            ++cullingStats_.lodCulledMeshes;
                                        }
                                        continue;
                                    }
                                    if (projectedRadiusPixels < lod2PixelThreshold_) {
                                        lodLevel = 2;
                                    } else if (projectedRadiusPixels < lod1PixelThreshold_) {
                                        lodLevel = 1;
                                    }
                                }

                                if (pipelineName == "pbrDeferred") {
                                    if (lodLevel == 1) {
                                        ++cullingStats_.lod1Meshes;
                                    } else if (lodLevel == 2) {
                                        ++cullingStats_.lod2Meshes;
                                    }
                                }

                                drawItems.push_back({models[j].get(), &mesh, queryMeshIndex,
                                                     distanceSquared, lodLevel});
                            }
                        }

                        // Front-to-back submission improves early depth rejection and makes the
                        // following-frame occlusion query results substantially more useful.
                        if (pipelineName == "pbrDeferred") {
                            std::sort(drawItems.begin(), drawItems.end(),
                                      [](const DrawItem& a, const DrawItem& b) {
                                          return a.distanceSquared < b.distanceSquared;
                                      });
                        }

                        const size_t visibleMeshCount = drawItems.size();
                        for (const DrawItem& item : drawItems) {
                            PbrPushConstants pushConstants;
                            pushConstants.model =
                                item.model->modelMatrix() * item.mesh->editorRenderTransform();
                            pushConstants.materialIndex = item.mesh->materialIndex_;
                            memcpy(pushConstants.coeffs, item.model->coeffs(),
                                   sizeof(pushConstants.coeffs));
                            vkCmdPushConstants(cmd, pipelines_.at(pipelineName)->pipelineLayout(),
                                               VK_SHADER_STAGE_VERTEX_BIT |
                                                   VK_SHADER_STAGE_FRAGMENT_BIT,
                                               0, sizeof(PbrPushConstants), &pushConstants);

                            vkCmdBindVertexBuffers(cmd, 0, 1, &item.mesh->vertexBuffer_, offsets);
                            vkCmdBindIndexBuffer(cmd, item.mesh->indexBuffer_,
                                                 item.mesh->lodIndexOffset(item.lodLevel),
                                                 VK_INDEX_TYPE_UINT32);
                            const uint32_t lodIndexCount =
                                item.mesh->lodIndexCount(item.lodLevel);

                            const bool issueOcclusionQuery =
                                pipelineName == "pbrDeferred" && occlusionCullingEnabled_ &&
                                occlusionQueryPool_ != VK_NULL_HANDLE &&
                                item.queryMeshIndex < meshQueryCount_;
                            if (issueOcclusionQuery) {
                                const uint32_t query =
                                    currentFrame * meshQueryCount_ +
                                    static_cast<uint32_t>(item.queryMeshIndex);
                                vkCmdBeginQuery(cmd, occlusionQueryPool_, query, 0);
                                vkCmdDrawIndexed(cmd, lodIndexCount, 1, 0, 0, 0);
                                vkCmdEndQuery(cmd, occlusionQueryPool_, query);
                                occlusionQueryIssued_[currentFrame][item.queryMeshIndex] = 1;
                            } else {
                                vkCmdDrawIndexed(cmd, lodIndexCount, 1, 0, 0, 0);
                            }
                        }

                        // Track rendering statistics
                        TRACY_PLOT("VisibleMeshes", static_cast<int64_t>(visibleMeshCount));
                        TRACY_PLOT("TotalMeshes", static_cast<int64_t>(totalMeshCount));
                        TRACY_PLOT("CulledMeshes",
                                   static_cast<int64_t>(totalMeshCount - visibleMeshCount));
                        if (pipelineName == "pbrDeferred") {
                            TRACY_PLOT(
                                "OcclusionCulledMeshes",
                                static_cast<int64_t>(cullingStats_.occlusionCulledMeshes));
                        }
                    }
                }
            }
        }

        {
            TRACY_CPU_SCOPE("End Rendering");
            vkCmdEndRendering(cmd);
        }
    }

    ++renderFrameCounter_;
}

void Renderer::createPipelines(const VkFormat swapChainColorFormat, const VkFormat depthFormat)
{
    TRACY_CPU_SCOPE("Renderer::createPipelines");

    {
        TRACY_CPU_SCOPE("Read Render Graph");
        if (!renderGraph_.readFromFile("RenderGraph.json")) {
            printLog("RenderGraph.json not found; using built-in deferred render graph");

            renderGraph_.addRenderNode({{"shadowMap"}, {}, "shadowMap", ""});
            renderGraph_.addRenderNode(
                {{"pbrDeferred"}, {"gAlbedo", "gNormal", "gPosition", "gMaterial"},
                 "depthStencil", ""});
            renderGraph_.addRenderNode({{"sky"}, {"floatColor1"}, "depthStencil", ""});
            renderGraph_.addRenderNode({{"deferredLighting"}, {}, "", ""});
            renderGraph_.addRenderNode({{"post"}, {"swapchain"}, "", ""});
        }
    }

    // Select optimal HDR format with proper priority (float formats first)
    VkFormat selectedHDRFormat =
        selectOptimalHDRFormat(false, false); // No alpha, moderate precision

    printLog("HDR Format Selection:");
    printLog("  Selected format: {} ({} bytes/pixel)", vkFormatToString(selectedHDRFormat),
             getFormatSize(selectedHDRFormat));

    {
        TRACY_CPU_SCOPE("Create Graphics Pipelines");
        // All pipelines use 1x samples (no MSAA) for educational simplicity
        pipelines_["pbrDeferred"] = std::make_unique<Pipeline>(
            ctx_, shaderManager_, PipelineConfig::createPbrDeferred(),
            vector<VkFormat>{VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT,
                             VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM},
            depthFormat, VK_SAMPLE_COUNT_1_BIT);

        pipelines_["sky"] = std::make_unique<Pipeline>(ctx_, shaderManager_, PipelineConfig::createSky(),
                                                  vector<VkFormat>{selectedHDRFormat}, depthFormat,
                                                  VK_SAMPLE_COUNT_1_BIT);

        pipelines_["post"] = std::make_unique<Pipeline>(ctx_, shaderManager_, PipelineConfig::createPost(),
                                                   vector<VkFormat>{swapChainColorFormat}, depthFormat,
                                                   VK_SAMPLE_COUNT_1_BIT);

        // Fix shadow map pipeline: depth-only pipelines should have no color attachments
        // Pass depth format as depthFormat parameter, not in outColorFormats
        pipelines_["shadowMap"] =
            std::make_unique<Pipeline>(ctx_, shaderManager_, PipelineConfig::createShadowMap(),
                                  vector<VkFormat>{}, VK_FORMAT_D16_UNORM, VK_SAMPLE_COUNT_1_BIT);
    }

    {
        TRACY_CPU_SCOPE("Create Compute Pipelines");
        // Fix deferred lighting pipeline: compute pipelines don't need color/depth formats
        pipelines_["deferredLighting"] =
            std::make_unique<Pipeline>(ctx_, shaderManager_, PipelineConfig::createDeferredLighting(),
                                  vector<VkFormat>{}, nullopt, VK_SAMPLE_COUNT_1_BIT);
    }

    // Store the selected format for texture creation
    selectedHDRFormat_ = selectedHDRFormat;
}

void Renderer::createTextures(uint32_t swapchainWidth, uint32_t swapchainHeight)
{
    TRACY_CPU_SCOPE("Renderer::createTextures");

    const char* lowSpecValue = std::getenv("HLAB_LOW_SPEC");
    const bool lowSpecMode = lowSpecValue != nullptr && string(lowSpecValue) != "0";
    if (lowSpecMode) {
        constexpr float kInternalRenderScale = 0.75f;
        swapchainWidth =
            std::max(1u, static_cast<uint32_t>(float(swapchainWidth) * kInternalRenderScale));
        swapchainHeight =
            std::max(1u, static_cast<uint32_t>(float(swapchainHeight) * kInternalRenderScale));
        printLog("Internal render resolution: {}x{} (75% scale)", swapchainWidth,
                 swapchainHeight);
    }

    {
        TRACY_CPU_SCOPE("createSamplers");
        samplerLinearRepeat_.createLinearRepeat();
        samplerLinearClamp_.createLinearClamp();
        samplerAnisoRepeat_.createAnisoRepeat();
        samplerAnisoClamp_.createAnisoClamp();
        samplerShadow_.createShadow();
    }

    // Initialize image buffers (simplified - no MSAA)
    const vector<string> imageNames = {"depthStencil", "floatColor1",    "floatColor2",
                                       "shadowMap",    "prefilteredMap", "irradianceMap",
                                       "brdfLut",      "gAlbedo",        "gNormal",
                                       "gPosition",    "gMaterial"};

    for (const auto& name : imageNames) {
        imageBuffers_[name] = std::make_unique<Image2D>(ctx_);
    }

    {
        TRACY_CPU_SCOPE("loadIBLTextures");
        // Load IBL textures for PBR rendering
        string path = kAssetsPathPrefix_ + "textures/golden_gate_hills_4k/";

        printLog("Loading IBL textures...");
        printLog("  Prefiltered: {}", path + "specularGGX.ktx2");
        printLog("  Irradiance: {}", path + "diffuseLambertian.ktx2");
        printLog("  BRDF LUT: {}", path + "outputLUT.png");

        const char* disableIblValue = std::getenv("HLAB_DISABLE_IBL");
        const bool useMinimalIbl =
            disableIblValue != nullptr && string(disableIblValue) != "0";

        if (useMinimalIbl) {
            printLog("Ultra-low-memory mode: using blue-sky fallback IBL textures");
            // Keep the fallback tiny for 2 GB GPUs, but use separate colors for the visible
            // sky and diffuse ambient light. A shared neutral gray made the whole scene dull.
            uint8_t skyEnvironment[4] = {65, 125, 205, 255};
            uint8_t ambientEnvironment[4] = {110, 130, 160, 255};
            uint8_t neutralBrdf[4] = {255, 255, 255, 255};
            imageBuffers_["prefilteredMap"]->createSolidCubemap(skyEnvironment);
            imageBuffers_["irradianceMap"]->createSolidCubemap(ambientEnvironment);
            imageBuffers_["brdfLut"]->createSolid(1, 1, neutralBrdf);
        } else {
            // Load prefiltered environment map (cubemap for specular reflections)
            imageBuffers_["prefilteredMap"]->createTextureFromKtx2(path + "specularGGX.ktx2",
                                                                  true);

            // Load irradiance map (cubemap for diffuse lighting)
            imageBuffers_["irradianceMap"]->createTextureFromKtx2(
                path + "diffuseLambertian.ktx2", true);

            // Load BRDF lookup table (2D texture)
            imageBuffers_["brdfLut"]->createTextureFromImage(path + "outputLUT.png", false, false);
        }

        imageBuffers_["prefilteredMap"]->setSampler(samplerLinearRepeat_.handle());
        imageBuffers_["irradianceMap"]->setSampler(samplerLinearRepeat_.handle());
        imageBuffers_["brdfLut"]->setSampler(samplerLinearClamp_.handle());
    }

    {
        TRACY_CPU_SCOPE("createHDRRenderTargets");
        // Create HDR render targets with selected format
        printLog("Creating HDR render targets:");
        printLog("  Format: {} ({} bytes/pixel)", vkFormatToString(selectedHDRFormat_),
                 getFormatSize(selectedHDRFormat_));

        // Log memory usage analysis
        logHDRMemoryUsage(swapchainWidth, swapchainHeight);

        // Storage color buffers for compute shaders and post-processing
        VkImageUsageFlags storageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                         VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        imageBuffers_["floatColor1"]->createImage(
            selectedHDRFormat_, swapchainWidth, swapchainHeight, VK_SAMPLE_COUNT_1_BIT,
            storageUsage, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 0, VK_IMAGE_VIEW_TYPE_2D);

        imageBuffers_["floatColor2"]->createImage(
            selectedHDRFormat_, swapchainWidth, swapchainHeight, VK_SAMPLE_COUNT_1_BIT,
            storageUsage, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 0, VK_IMAGE_VIEW_TYPE_2D);
    }

    {
        TRACY_CPU_SCOPE("createGBuffer");
        // Create G-buffer textures for deferred rendering
        printLog("Creating G-buffer textures for deferred rendering:");

        // G-Buffer format selection for optimal memory usage and precision
        VkFormat albedoFormat = VK_FORMAT_R8G8B8A8_UNORM; // Albedo + Metallic (4 bytes)
        VkFormat normalFormat =
            VK_FORMAT_R16G16B16A16_SFLOAT; // Normal + Roughness (8 bytes, needs precision)
        VkFormat positionFormat =
            VK_FORMAT_R16G16B16A16_SFLOAT; // Position + Depth (8 bytes, adequate for this scene)
        VkFormat materialFormat = VK_FORMAT_R8G8B8A8_UNORM; // AO + Emissive + Material ID (4 bytes)

        printLog("  gAlbedo: {} ({} bytes/pixel)", vkFormatToString(albedoFormat),
                 getFormatSize(albedoFormat));
        printLog("  gNormal: {} ({} bytes/pixel)", vkFormatToString(normalFormat),
                 getFormatSize(normalFormat));
        printLog("  gPosition: {} ({} bytes/pixel)", vkFormatToString(positionFormat),
                 getFormatSize(positionFormat));
        printLog("  gMaterial: {} ({} bytes/pixel)", vkFormatToString(materialFormat),
                 getFormatSize(materialFormat));

        // G-buffer usage flags (similar to floatColor but without storage bit since they're render
        // targets)
        VkImageUsageFlags gBufferUsage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        // Create gAlbedo buffer (Albedo RGB + Metallic A)
        imageBuffers_["gAlbedo"]->createImage(
            albedoFormat, swapchainWidth, swapchainHeight, VK_SAMPLE_COUNT_1_BIT, gBufferUsage,
            VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 0, VK_IMAGE_VIEW_TYPE_2D);
        imageBuffers_["gAlbedo"]->setSampler(samplerLinearClamp_.handle());

        // Create gNormal buffer (World Normal RGB + Roughness A)
        imageBuffers_["gNormal"]->createImage(
            normalFormat, swapchainWidth, swapchainHeight, VK_SAMPLE_COUNT_1_BIT, gBufferUsage,
            VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 0, VK_IMAGE_VIEW_TYPE_2D);
        imageBuffers_["gNormal"]->setSampler(samplerLinearClamp_.handle());

        // Create gPosition buffer (World Position RGB + Depth A)
        imageBuffers_["gPosition"]->createImage(
            positionFormat, swapchainWidth, swapchainHeight, VK_SAMPLE_COUNT_1_BIT, gBufferUsage,
            VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 0, VK_IMAGE_VIEW_TYPE_2D);
        imageBuffers_["gPosition"]->setSampler(samplerLinearClamp_.handle());

        // Create gMaterial buffer (AO R + Emissive Intensity G + Material ID B + Unused A)
        imageBuffers_["gMaterial"]->createImage(
            materialFormat, swapchainWidth, swapchainHeight, VK_SAMPLE_COUNT_1_BIT, gBufferUsage,
            VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, 0, VK_IMAGE_VIEW_TYPE_2D);
        imageBuffers_["gMaterial"]->setSampler(samplerLinearClamp_.handle());

        printLog("G-buffer creation complete");
    }

    {
        TRACY_CPU_SCOPE("createDepthAndShadowBuffers");
        // Create depth buffer (no MSAA)
        imageBuffers_["depthStencil"]->createDepthBuffer(swapchainWidth, swapchainHeight);

        // Create shadow map
        const uint32_t shadowMapSize = lowSpecMode ? 1024u : 4096u;
        printLog("Shadow map resolution: {}x{}", shadowMapSize, shadowMapSize);
        imageBuffers_["shadowMap"]->createShadow(shadowMapSize, shadowMapSize);
        imageBuffers_["shadowMap"]->setSampler(samplerShadow_.handle());
    }

    {
        TRACY_CPU_SCOPE("setSamplers");
        // Set samplers for storage and sampling images
        imageBuffers_["floatColor1"]->setSampler(samplerLinearRepeat_.handle());
        imageBuffers_["floatColor2"]->setSampler(samplerLinearRepeat_.handle());
        imageBuffers_["depthStencil"]->setSampler(samplerLinearClamp_.handle());
    }
}

// Format selection function with proper priority: float formats first, R8G8B8A8 last
VkFormat Renderer::selectOptimalHDRFormat(bool needsAlpha, bool fullPrecision)
{
    TRACY_CPU_SCOPE("Renderer::selectOptimalHDRFormat");

    vector<VkFormat> candidateFormats;

    if (!needsAlpha && !fullPrecision) {
        // Memory-efficient HDR formats (no alpha, moderate precision)
        candidateFormats = {
            VK_FORMAT_B10G11R11_UFLOAT_PACK32, // 4 bytes - 50% savings, packed float (correct
                                               // format)
            VK_FORMAT_R16G16B16_SFLOAT,        // 6 bytes - 25% savings, half precision
            VK_FORMAT_R16G16B16A16_SFLOAT,     // 8 bytes - standard HDR with alpha
            VK_FORMAT_R32G32B32A32_SFLOAT,        // 12 bytes - full precision RGB
            // R8G8B8A8_UNORM is LAST - not a float format, poor for HDR
            VK_FORMAT_R8G8B8A8_UNORM // 4 bytes - NOT FLOAT, last resort
        };
    } else if (!fullPrecision) {
        // Standard HDR with alpha channel
        candidateFormats = {
            VK_FORMAT_R16G16B16A16_SFLOAT, // 8 bytes - standard HDR
            VK_FORMAT_R32G32B32A32_SFLOAT, // 16 bytes - full precision
            // R8G8B8A8_UNORM is LAST - not suitable for HDR
            VK_FORMAT_R8G8B8A8_UNORM // 4 bytes - NOT FLOAT, last resort
        };
    } else {
        // Full precision required
        candidateFormats = {
            VK_FORMAT_R32G32B32A32_SFLOAT, // 16 bytes - full precision
            VK_FORMAT_R32G32B32_SFLOAT,    // 12 bytes - full precision RGB
            VK_FORMAT_R16G16B16A16_SFLOAT, // 8 bytes - half precision fallback
            // R8G8B8A8_UNORM is LAST - inadequate for full precision HDR
            VK_FORMAT_R8G8B8A8_UNORM // 4 bytes - NOT FLOAT, emergency fallback
        };
    }

    // Test each format for compatibility (float formats first, R8G8B8A8 last)
    for (size_t i = 0; i < candidateFormats.size(); ++i) {
        VkFormat format = candidateFormats[i];

        if (isFormatSuitableForHDR(format)) {
            string formatType = (format == VK_FORMAT_R8G8B8A8_UNORM) ? "NON-FLOAT" : "FLOAT";
            float memoryRatio = static_cast<float>(getFormatSize(format)) / 8.0f; // vs RGBA16F

            printLog("✓ Selected HDR format: {} ({} bytes/pixel, {}, {:.0f}% memory vs RGBA16F)",
                     vkFormatToString(format), getFormatSize(format), formatType,
                     memoryRatio * 100.0f);

            // Warn if we fell back to non-float format
            if (format == VK_FORMAT_R8G8B8A8_UNORM) {
                printLog("⚠ WARNING: Using R8G8B8A8_UNORM for HDR - limited dynamic range!");
                printLog("  Consider using float formats for better HDR quality");
            }

            return format;
        } else {
            string formatType = (format == VK_FORMAT_R8G8B8A8_UNORM) ? "NON-FLOAT" : "FLOAT";
            printLog("✗ Format {} ({}) not supported, trying next...", vkFormatToString(format),
                     formatType);
        }
    }

    // Emergency fallback - this should rarely happen
    printLog(
        "⚠ All candidate formats failed, using emergency fallback: VK_FORMAT_R16G16B16A16_SFLOAT");
    return VK_FORMAT_R16G16B16A16_SFLOAT;
}

// Enhanced format validation function
bool Renderer::isFormatSuitableForHDR(VkFormat format)
{
    TRACY_CPU_SCOPE("Renderer::isFormatSuitableForHDR");

    // Check if format supports required features for HDR rendering
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(ctx_.physicalDevice(), format, &props);

    // Required features for HDR color attachments
    VkFormatFeatureFlags requiredFeatures =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | // Can render to it
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;     // Can sample from it

    // Optional but preferred for HDR
    VkFormatFeatureFlags preferredFeatures =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT; // Can blend (for transparency)

    bool hasRequired = (props.optimalTilingFeatures & requiredFeatures) == requiredFeatures;
    bool hasPreferred = (props.optimalTilingFeatures & preferredFeatures) == preferredFeatures;

    if (hasRequired && !hasPreferred && format != VK_FORMAT_R8G8B8A8_UNORM) {
        printLog("  Note: {} missing blend support but acceptable for HDR",
                 vkFormatToString(format));
    }

    return hasRequired;
}

void Renderer::logHDRMemoryUsage(uint32_t width, uint32_t height)
{
    TRACY_CPU_SCOPE("Renderer::logHDRMemoryUsage");

    uint64_t totalPixels = static_cast<uint64_t>(width) * height;

    uint32_t hdrBytes = getFormatSize(selectedHDRFormat_);
    uint32_t standardBytes = getFormatSize(VK_FORMAT_R16G16B16A16_SFLOAT);

    // Calculate memory usage for current format (no MSAA, so 1x samples)
    uint64_t hdrMemoryMB = (totalPixels * hdrBytes + totalPixels * hdrBytes * 2) / (1024 * 1024);
    uint64_t standardMemoryMB =
        (totalPixels * standardBytes + totalPixels * standardBytes * 2) / (1024 * 1024);

    float savings =
        (1.0f - static_cast<float>(hdrMemoryMB) / static_cast<float>(standardMemoryMB)) * 100.0f;

    printLog("HDR Memory Analysis:");
    printLog("  Resolution: {}x{} (no MSAA)", width, height);
    printLog("  Current format memory: {} MB", hdrMemoryMB);
    printLog("  Standard RGBA16F memory: {} MB", standardMemoryMB);
    if (savings > 0) {
        printLog("  Memory savings: {:.1f}%", savings);
    } else {
        printLog("  Memory overhead: {:.1f}%", -savings);
    }

    // Quality assessment
    if (selectedHDRFormat_ == VK_FORMAT_R8G8B8A8_UNORM) {
        printLog("  Quality: ⚠ LIMITED - R8G8B8A8 has restricted HDR range");
    } else if (selectedHDRFormat_ == VK_FORMAT_B10G11R11_UFLOAT_PACK32) {
        printLog("  Quality: ✓ GOOD - B10G11R11 excellent for HDR with memory savings");
    } else if (selectedHDRFormat_ == VK_FORMAT_R16G16B16A16_SFLOAT) {
        printLog("  Quality: ✓ EXCELLENT - Standard HDR format");
    } else {
        printLog("  Quality: ✓ HIGH - Float format suitable for HDR");
    }
}

void Renderer::updateViewFrustum(const glm::mat4& viewProjection)
{
    TRACY_CPU_SCOPE("Renderer::updateViewFrustum");

    if (frustumCullingEnabled_) {
        viewFrustum_.extractFromViewProjection(viewProjection);
    }
}

void Renderer::performFrustumCulling(vector<unique_ptr<Model>>& models)
{
    TRACY_CPU_SCOPE("Renderer::performFrustumCulling");

    cullingStats_.totalMeshes = 0;
    cullingStats_.culledMeshes = 0;
    cullingStats_.renderedMeshes = 0;

    if (!frustumCullingEnabled_) {
        TRACY_CPU_SCOPE("Frustum Culling Disabled");
        for (auto& model : models) {
            for (auto& mesh : model->meshes()) {
                mesh.isCulled = false;
                cullingStats_.totalMeshes++;
                cullingStats_.renderedMeshes++;
            }
        }
        return;
    }

    {
        TRACY_CPU_SCOPE("frustumCullingLoop");
        for (auto& model : models) {
            for (auto& mesh : model->meshes()) {
                cullingStats_.totalMeshes++;

                bool isVisible = viewFrustum_.intersects(mesh.worldBounds);

                mesh.isCulled = !isVisible;

                if (isVisible) {
                    cullingStats_.renderedMeshes++;
                } else {
                    cullingStats_.culledMeshes++;
                }
            }
        }
    }

    // Track culling statistics in Tracy
    TRACY_PLOT("FrustumCulling_TotalMeshes", static_cast<int64_t>(cullingStats_.totalMeshes));
    TRACY_PLOT("FrustumCulling_RenderedMeshes", static_cast<int64_t>(cullingStats_.renderedMeshes));
    TRACY_PLOT("FrustumCulling_CulledMeshes", static_cast<int64_t>(cullingStats_.culledMeshes));

    if (cullingStats_.totalMeshes > 0) {
        float cullingEfficiency =
            (float(cullingStats_.culledMeshes) / float(cullingStats_.totalMeshes)) * 100.0f;
        TRACY_PLOT("FrustumCulling_EfficiencyPercent", static_cast<int64_t>(cullingEfficiency));
    }
}

void Renderer::updateWorldBounds(vector<unique_ptr<Model>>& models)
{
    TRACY_CPU_SCOPE("Renderer::updateWorldBounds");

    if (cachedModelMatrices_.size() != models.size()) {
        cachedModelMatrices_.assign(models.size(), glm::mat4(1.0f));
        worldBoundsValid_.assign(models.size(), uint8_t{0});
    }

    for (size_t modelIndex = 0; modelIndex < models.size(); ++modelIndex) {
        auto& model = models[modelIndex];
        const glm::mat4& modelMatrix = model->modelMatrix();

        const bool modelTransformChanged =
            !worldBoundsValid_[modelIndex] || cachedModelMatrices_[modelIndex] != modelMatrix;

        // Static meshes keep their cached AABBs. A gizmo edit only invalidates the
        // selected mesh instead of rebuilding all 3,000+ Bistro bounds.
        for (auto& mesh : model->meshes()) {
            if (modelTransformChanged || mesh.editorTransformDirty) {
                mesh.updateWorldBounds(modelMatrix * mesh.editorRenderTransform());
                mesh.editorTransformDirty = false;
            }
        }

        cachedModelMatrices_[modelIndex] = modelMatrix;
        worldBoundsValid_[modelIndex] = 1;
    }
}

void Renderer::setFrustumCullingEnabled(bool enabled)
{
    frustumCullingEnabled_ = enabled;
}

bool Renderer::isFrustumCullingEnabled() const
{
    return frustumCullingEnabled_;
}

void Renderer::setOcclusionCullingEnabled(bool enabled)
{
    occlusionCullingEnabled_ = enabled;
    if (!enabled) {
        std::fill(occlusionVisible_.begin(), occlusionVisible_.end(), uint8_t{1});
        std::fill(occlusionMissCounts_.begin(), occlusionMissCounts_.end(), uint8_t{0});
    }
}

bool Renderer::isOcclusionCullingEnabled() const
{
    return occlusionCullingEnabled_;
}

const CullingStats& Renderer::getCullingStats() const
{
    return cullingStats_;
}

VkRenderingAttachmentInfo Renderer::createColorAttachment(VkImageView imageView,
                                                          VkAttachmentLoadOp loadOp,
                                                          VkClearColorValue clearColor,
                                                          VkImageView resolveImageView,
                                                          VkResolveModeFlagBits resolveMode) const
{
    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = imageView;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = loadOp;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = clearColor;
    attachment.resolveMode = resolveMode;
    attachment.resolveImageView = resolveImageView;
    attachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    return attachment;
}

VkRenderingAttachmentInfo Renderer::createDepthAttachment(VkImageView imageView,
                                                          VkAttachmentLoadOp loadOp,
                                                          float clearDepth,
                                                          VkImageView resolveImageView,
                                                          VkResolveModeFlagBits resolveMode) const
{
    VkRenderingAttachmentInfo attachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    attachment.imageView = imageView;
    attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachment.loadOp = loadOp;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.clearValue.depthStencil = {clearDepth, 0};
    attachment.resolveMode = resolveMode;
    attachment.resolveImageView = resolveImageView;
    attachment.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    return attachment;
}

} // namespace hlab