#include "GuiRenderer.h"
#include "PipelineConfig.h"

#include <filesystem>

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

    // Compact portfolio inspector: dark navy surfaces with a restrained blue accent.
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(14.0f, 14.0f);
    style.FramePadding = ImVec2(9.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 11.0f;
    style.GrabMinSize = 10.0f;
    style.WindowRounding = 8.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 5.0f;
    style.TabRounding = 5.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = ImVec4(0.90f, 0.93f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.54f, 0.64f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.055f, 0.090f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.045f, 0.070f, 0.110f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.040f, 0.060f, 0.095f, 1.00f);
    colors[ImGuiCol_Border] = ImVec4(0.16f, 0.22f, 0.32f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.075f, 0.105f, 0.155f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.10f, 0.16f, 0.24f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.12f, 0.20f, 0.30f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.035f, 0.055f, 0.090f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.045f, 0.075f, 0.120f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.29f, 0.60f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.25f, 0.52f, 0.90f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.37f, 0.68f, 1.00f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.10f, 0.17f, 0.26f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.15f, 0.30f, 0.48f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.40f, 0.66f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.09f, 0.15f, 0.23f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.14f, 0.28f, 0.45f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.17f, 0.37f, 0.61f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.14f, 0.20f, 0.29f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.060f, 0.090f, 0.135f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.16f, 0.33f, 0.54f, 1.00f);
    colors[ImGuiCol_TabActive] = ImVec4(0.12f, 0.25f, 0.42f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.025f, 0.040f, 0.065f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.16f, 0.23f, 0.33f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.23f, 0.34f, 0.48f, 1.00f);

    // Keep the same visual hierarchy while making the complete editor palette
    // substantially calmer.
    for (int colorIndex = 0; colorIndex < ImGuiCol_COUNT; ++colorIndex) {
        float hue = 0.0f;
        float saturation = 0.0f;
        float value = 0.0f;
        ImGui::ColorConvertRGBtoHSV(colors[colorIndex].x, colors[colorIndex].y,
                                    colors[colorIndex].z, hue, saturation, value);
        saturation *= 0.50f;
        value *= 0.50f;
        ImGui::ColorConvertHSVtoRGB(hue, saturation, value, colors[colorIndex].x,
                                    colors[colorIndex].y, colors[colorIndex].z);
    }

    style.ScaleAllSizes(scale_);
    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = scale_;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    {
        const string fontFileName =
            "../../assets/Noto_Sans_KR/static/NotoSansKR-SemiBold.ttf";

        unsigned char* fontData = nullptr;
        int texWidth, texHeight;
        ImGuiIO& io = ImGui::GetIO();

        ImFontConfig config;
        config.MergeMode = false;

        if (std::filesystem::exists(fontFileName)) {
            io.Fonts->AddFontFromFileTTF(fontFileName.c_str(), 16.0f * scale_, &config,
                                         io.Fonts->GetGlyphRangesDefault());

            config.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(fontFileName.c_str(), 16.0f * scale_, &config,
                                         io.Fonts->GetGlyphRangesKorean());
        } else {
            printLog("Optional Korean font not found: {}. Using ImGui default font.",
                     fontFileName);
            io.Fonts->AddFontDefault();
        }

        io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
        if (!fontData) {
            exitWithMessage("Failed to create ImGui font atlas");
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
        VkDeviceSize newCapacity =
            std::max(static_cast<VkDeviceSize>(vertexBufferSize * 1.5f),
                     static_cast<VkDeviceSize>(512 * sizeof(ImDrawVert)));

        frame.vertexBuffer.createVertexBuffer(newCapacity, nullptr);
        updateCmdBuffers = true;
    }

    if ((frame.indexBuffer.buffer() == VK_NULL_HANDLE) ||
        (indexBufferSize > frame.indexBuffer.allocatedSize())) {
        VkDeviceSize newCapacity =
            std::max(static_cast<VkDeviceSize>(indexBufferSize * 1.5f),
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