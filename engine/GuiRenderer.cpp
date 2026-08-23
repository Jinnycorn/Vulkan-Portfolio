#include "GuiRenderer.h"
#include "PipelineConfig.h"

namespace hlab {

GuiRenderer::GuiRenderer(Context& ctx, ShaderManager& shaderManager, VkFormat colorFormat, uint32_t maxFramesInFlight)
    : ctx_(ctx), shaderManager_(shaderManager),
      fontImage_(make_unique<Image2D>(ctx)), fontSampler_(ctx), pushConsts_(ctx),
      guiPipeline_(ctx, shaderManager_, PipelineConfig::createGui(), {colorFormat})
{
    frameData_.reserve(maxFramesInFlight);
    for (uint32_t i = 0; i < maxFramesInFlight; i++) {
        frameData_.push_back(make_unique<FrameData>(ctx));
    }

    pushConsts_.setStageFlags(VK_SHADER_STAGE_VERTEX_BIT);

    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.72f;
    style.WindowPadding = ImVec2(16.0f, 14.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.CellPadding = ImVec2(9.0f, 7.0f);
    style.ItemSpacing = ImVec2(10.0f, 9.0f);
    style.ItemInnerSpacing = ImVec2(7.0f, 6.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 11.0f;
    style.WindowRounding = 9.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]                 = ImVec4(0.94f, 0.96f, 0.98f, 1.00f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.52f, 0.58f, 0.62f, 1.00f);
    c[ImGuiCol_WindowBg]             = ImVec4(0.045f, 0.052f, 0.060f, 1.00f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.060f, 0.070f, 0.078f, 1.00f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.050f, 0.060f, 0.068f, 1.00f);
    c[ImGuiCol_Border]               = ImVec4(0.15f, 0.18f, 0.20f, 1.00f);
    c[ImGuiCol_FrameBg]              = ImVec4(0.085f, 0.100f, 0.110f, 1.00f);
    c[ImGuiCol_FrameBgHovered]       = ImVec4(0.11f, 0.16f, 0.16f, 1.00f);
    c[ImGuiCol_FrameBgActive]        = ImVec4(0.13f, 0.22f, 0.21f, 1.00f);
    c[ImGuiCol_TitleBg]              = ImVec4(0.035f, 0.042f, 0.048f, 1.00f);
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.055f, 0.075f, 0.075f, 1.00f);
    c[ImGuiCol_MenuBarBg]            = ImVec4(0.028f, 0.034f, 0.040f, 1.00f);
    c[ImGuiCol_CheckMark]            = ImVec4(0.26f, 0.88f, 0.77f, 1.00f);
    c[ImGuiCol_SliderGrab]           = ImVec4(0.22f, 0.72f, 0.66f, 1.00f);
    c[ImGuiCol_SliderGrabActive]     = ImVec4(0.30f, 0.92f, 0.82f, 1.00f);
    c[ImGuiCol_Button]               = ImVec4(0.10f, 0.14f, 0.15f, 1.00f);
    c[ImGuiCol_ButtonHovered]        = ImVec4(0.13f, 0.29f, 0.27f, 1.00f);
    c[ImGuiCol_ButtonActive]         = ImVec4(0.15f, 0.41f, 0.37f, 1.00f);
    c[ImGuiCol_Header]               = ImVec4(0.09f, 0.12f, 0.13f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.12f, 0.27f, 0.25f, 1.00f);
    c[ImGuiCol_HeaderActive]         = ImVec4(0.14f, 0.39f, 0.35f, 1.00f);
    c[ImGuiCol_Separator]            = ImVec4(0.14f, 0.17f, 0.18f, 1.00f);
    c[ImGuiCol_Tab]                  = ImVec4(0.06f, 0.075f, 0.082f, 1.00f);
    c[ImGuiCol_TabHovered]           = ImVec4(0.12f, 0.31f, 0.29f, 1.00f);
    c[ImGuiCol_TabActive]            = ImVec4(0.10f, 0.24f, 0.22f, 1.00f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.035f, 0.042f, 0.048f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.16f, 0.20f, 0.21f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.22f, 0.35f, 0.34f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.26f, 0.48f, 0.44f, 1.00f);

    style.ScaleAllSizes(scale_);
    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = scale_;

    {
        const string fontFileName = "../../assets/Noto_Sans_KR/static/NotoSansKR-SemiBold.ttf";
        unsigned char* fontData = nullptr;
        int texWidth, texHeight;
        ImFontConfig config;
        config.MergeMode = false;

        io.Fonts->AddFontFromFileTTF(fontFileName.c_str(), 17.0f * scale_, &config,
                                     io.Fonts->GetGlyphRangesDefault());
        config.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(fontFileName.c_str(), 17.0f * scale_, &config,
                                     io.Fonts->GetGlyphRangesKorean());
        io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
        if (!fontData) {
            exitWithMessage("Failed to load font data from: {}", fontFileName);
        }
        fontImage_->createFromPixelData(fontData, texWidth, texHeight, 4, false);
    }

    fontSampler_.createAnisoRepeat();
    fontImage_->setSampler(fontSampler_.handle());
    fontSet_.create(ctx_, guiPipeline_.layouts()[0], {*fontImage_});
}

GuiRenderer::~GuiRenderer()
{
    if (ImGui::GetCurrentContext()) {
        ImGui::DestroyContext();
    }
}

auto GuiRenderer::imguiPipeline() -> Pipeline&
{
    return guiPipeline_;
}

bool GuiRenderer::update(uint32_t frameIndex)
{
    ImDrawData* imDrawData = ImGui::GetDrawData();
    if (!imDrawData || imDrawData->TotalVtxCount == 0 || imDrawData->TotalIdxCount == 0) {
        return false;
    }

    auto& frame = *frameData_[frameIndex % frameData_.size()];
    bool updateCmdBuffers = false;
    VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
    VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);
    vertexCount_ = imDrawData->TotalVtxCount;
    indexCount_ = imDrawData->TotalIdxCount;

    if ((frame.vertexBuffer.buffer() == VK_NULL_HANDLE) ||
        (vertexBufferSize > frame.vertexBuffer.allocatedSize())) {
        VkDeviceSize newCapacity = std::max(static_cast<VkDeviceSize>(vertexBufferSize * 1.5f),
                                            static_cast<VkDeviceSize>(512 * sizeof(ImDrawVert)));
        frame.vertexBuffer.createVertexBuffer(newCapacity, nullptr);
        updateCmdBuffers = true;
    }

    if ((frame.indexBuffer.buffer() == VK_NULL_HANDLE) ||
        (indexBufferSize > frame.indexBuffer.allocatedSize())) {
        VkDeviceSize newCapacity = std::max(static_cast<VkDeviceSize>(indexBufferSize * 1.5f),
                                            static_cast<VkDeviceSize>(1024 * sizeof(ImDrawIdx)));
        frame.indexBuffer.createIndexBuffer(newCapacity, nullptr);
        updateCmdBuffers = true;
    }

    ImDrawVert* vtxDst = (ImDrawVert*)frame.vertexBuffer.mapped();
    ImDrawIdx* idxDst = (ImDrawIdx*)frame.indexBuffer.mapped();
    for (int n = 0; n < imDrawData->CmdListsCount; n++) {
        const ImDrawList* cmd_list = imDrawData->CmdLists[n];
        memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtxDst += cmd_list->VtxBuffer.Size;
        idxDst += cmd_list->IdxBuffer.Size;
    }

    frame.vertexBuffer.flush();
    frame.indexBuffer.flush();
    return updateCmdBuffers;
}

void GuiRenderer::draw(const VkCommandBuffer cmd, VkImageView swapchainImageView,
                       VkViewport viewport, uint32_t frameIndex)
{
    VkRenderingAttachmentInfo swapchainColorAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    swapchainColorAttachment.imageView = swapchainImageView;
    swapchainColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swapchainColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    swapchainColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo colorOnlyRenderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    colorOnlyRenderingInfo.renderArea = {0, 0, uint32_t(viewport.width), uint32_t(viewport.height)};
    colorOnlyRenderingInfo.layerCount = 1;
    colorOnlyRenderingInfo.colorAttachmentCount = 1;
    colorOnlyRenderingInfo.pColorAttachments = &swapchainColorAttachment;

    ImDrawData* imDrawData = ImGui::GetDrawData();
    if ((!imDrawData) || (imDrawData->CmdListsCount == 0)) {
        return;
    }

    auto& frame = *frameData_[frameIndex % frameData_.size()];
    vkCmdBeginRendering(cmd, &colorOnlyRenderingInfo);
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    const auto descriptorSet = fontSet_.handle();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, guiPipeline_.pipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, guiPipeline_.pipelineLayout(), 0,
                            1, &descriptorSet, 0, nullptr);

    ImGuiIO& io = ImGui::GetIO();
    auto& pc = pushConsts_.data();
    pc.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
    pc.translate = glm::vec2(-1.0f);
    pushConsts_.push(cmd, guiPipeline_.pipelineLayout());

    VkDeviceSize offsets[1] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, &frame.vertexBuffer.buffer(), offsets);
    vkCmdBindIndexBuffer(cmd, frame.indexBuffer.buffer(), 0, VK_INDEX_TYPE_UINT16);

    int32_t vertexOffset = 0;
    int32_t indexOffset = 0;
    for (int32_t i = 0; i < imDrawData->CmdListsCount; i++) {
        const ImDrawList* cmd_list = imDrawData->CmdLists[i];
        for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];
            VkRect2D scissorRect;
            scissorRect.offset.x = std::max((int32_t)(pcmd->ClipRect.x), 0);
            scissorRect.offset.y = std::max((int32_t)(pcmd->ClipRect.y), 0);
            scissorRect.extent.width = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
            scissorRect.extent.height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y);
            vkCmdSetScissor(cmd, 0, 1, &scissorRect);
            vkCmdDrawIndexed(cmd, pcmd->ElemCount, 1, indexOffset, vertexOffset, 0);
            indexOffset += pcmd->ElemCount;
        }
        vertexOffset += cmd_list->VtxBuffer.Size;
    }

    vkCmdEndRendering(cmd);
}

void GuiRenderer::resize(uint32_t width, uint32_t height)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)(width), (float)(height));
}

} // namespace hlab
