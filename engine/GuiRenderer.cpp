#include "GuiRenderer.h"
#include "PipelineConfig.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace hlab {

GuiRenderer::GuiRenderer(Context& ctx, ShaderManager& shaderManager, VkFormat colorFormat,
                         uint32_t maxFramesInFlight)
    : ctx_(ctx), shaderManager_(shaderManager),
      fontImage_(make_unique<Image2D>(ctx)), fontSampler_(ctx), pushConsts_(ctx),
      guiPipeline_(ctx, shaderManager_, PipelineConfig::createGui(), {colorFormat})
{
    frameData_.reserve(maxFramesInFlight);
    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        frameData_.push_back(make_unique<FrameData>(ctx));
    }

    pushConsts_.setStageFlags(VK_SHADER_STAGE_VERTEX_BIT);

    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.Alpha = 1.0f;
    style.DisabledAlpha = 1.0f;
    style.WindowPadding = ImVec2(16.0f, 14.0f);
    style.FramePadding = ImVec2(11.0f, 7.0f);
    style.CellPadding = ImVec2(10.0f, 7.0f);
    style.ItemSpacing = ImVec2(10.0f, 9.0f);
    style.ItemInnerSpacing = ImVec2(7.0f, 6.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 13.0f;
    style.GrabMinSize = 12.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;

    style.WindowRounding = 10.0f;
    style.ChildRounding = 9.0f;
    style.FrameRounding = 7.0f;
    style.PopupRounding = 9.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding = 7.0f;
    style.TabRounding = 7.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                 = ImVec4(0.94f, 0.96f, 0.97f, 1.00f);
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.58f, 0.63f, 0.67f, 1.00f);
    colors[ImGuiCol_WindowBg]             = ImVec4(0.045f, 0.052f, 0.060f, 1.00f);
    colors[ImGuiCol_ChildBg]              = ImVec4(0.060f, 0.070f, 0.078f, 1.00f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.050f, 0.060f, 0.068f, 1.00f);
    colors[ImGuiCol_MenuBarBg]            = ImVec4(0.028f, 0.034f, 0.040f, 1.00f);
    colors[ImGuiCol_TitleBg]              = ImVec4(0.035f, 0.042f, 0.048f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.055f, 0.075f, 0.075f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.035f, 0.042f, 0.048f, 1.00f);
    colors[ImGuiCol_Border]               = ImVec4(0.15f, 0.18f, 0.20f, 1.00f);
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]              = ImVec4(0.085f, 0.100f, 0.110f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.11f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.13f, 0.22f, 0.21f, 1.00f);
    colors[ImGuiCol_CheckMark]            = ImVec4(0.26f, 0.88f, 0.77f, 1.00f);
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.22f, 0.72f, 0.66f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.30f, 0.92f, 0.82f, 1.00f);
    colors[ImGuiCol_Button]               = ImVec4(0.10f, 0.14f, 0.15f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.13f, 0.29f, 0.27f, 1.00f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.15f, 0.41f, 0.37f, 1.00f);
    colors[ImGuiCol_Header]               = ImVec4(0.09f, 0.12f, 0.13f, 1.00f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.12f, 0.27f, 0.25f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.14f, 0.39f, 0.35f, 1.00f);
    colors[ImGuiCol_Separator]            = ImVec4(0.14f, 0.17f, 0.18f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.20f, 0.72f, 0.67f, 1.00f);
    colors[ImGuiCol_SeparatorActive]      = ImVec4(0.28f, 0.86f, 0.78f, 1.00f);
    colors[ImGuiCol_Tab]                  = ImVec4(0.06f, 0.075f, 0.082f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.12f, 0.31f, 0.29f, 1.00f);
    colors[ImGuiCol_TabActive]            = ImVec4(0.10f, 0.24f, 0.22f, 1.00f);
    colors[ImGuiCol_TabUnfocused]         = ImVec4(0.045f, 0.055f, 0.062f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.075f, 0.125f, 0.125f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.035f, 0.042f, 0.048f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.16f, 0.20f, 0.21f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.22f, 0.35f, 0.34f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.26f, 0.48f, 0.44f, 1.00f);
    colors[ImGuiCol_ResizeGrip]           = ImVec4(0.17f, 0.34f, 0.32f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.23f, 0.62f, 0.57f, 1.00f);
    colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.30f, 0.86f, 0.77f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.075f, 0.090f, 0.098f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.17f, 0.20f, 0.21f, 1.00f);
    colors[ImGuiCol_TableBorderLight]     = ImVec4(0.11f, 0.13f, 0.14f, 1.00f);
    colors[ImGuiCol_TableRowBg]           = ImVec4(0.055f, 0.063f, 0.070f, 1.00f);
    colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.075f, 0.085f, 0.092f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.12f, 0.42f, 0.38f, 1.00f);
    colors[ImGuiCol_PlotLines]            = ImVec4(0.34f, 0.86f, 0.78f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]     = ImVec4(0.46f, 1.00f, 0.90f, 1.00f);
    colors[ImGuiCol_PlotHistogram]        = ImVec4(0.26f, 0.76f, 0.69f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.42f, 0.94f, 0.84f, 1.00f);
    colors[ImGuiCol_DragDropTarget]       = ImVec4(0.36f, 1.00f, 0.85f, 1.00f);
    colors[ImGuiCol_NavHighlight]         = ImVec4(0.28f, 0.88f, 0.78f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]= ImVec4(0.72f, 0.96f, 0.91f, 1.00f);
    colors[ImGuiCol_NavWindowingDimBg]    = ImVec4(0.02f, 0.025f, 0.030f, 1.00f);
    colors[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.02f, 0.025f, 0.030f, 1.00f);

    style.ScaleAllSizes(scale_);

    ImGuiIO& io = ImGui::GetIO();
    io.FontGlobalScale = scale_;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    {
        const string fontFileName =
            "../../assets/Noto_Sans_KR/static/NotoSansKR-SemiBold.ttf";
        unsigned char* fontData = nullptr;
        int texWidth = 0;
        int texHeight = 0;

        ImFontConfig config;
        config.MergeMode = false;

        if (std::filesystem::exists(fontFileName)) {
            io.Fonts->AddFontFromFileTTF(fontFileName.c_str(), 17.0f * scale_, &config,
                                         io.Fonts->GetGlyphRangesDefault());
            config.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(fontFileName.c_str(), 17.0f * scale_, &config,
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

    const VkDeviceSize vertexBufferSize =
        VkDeviceSize(imDrawData->TotalVtxCount) * sizeof(ImDrawVert);
    const VkDeviceSize indexBufferSize =
        VkDeviceSize(imDrawData->TotalIdxCount) * sizeof(ImDrawIdx);

    vertexCount_ = imDrawData->TotalVtxCount;
    indexCount_ = imDrawData->TotalIdxCount;

    if (frame.vertexBuffer.buffer() == VK_NULL_HANDLE ||
        vertexBufferSize > frame.vertexBuffer.allocatedSize()) {
        const VkDeviceSize newCapacity =
            std::max(static_cast<VkDeviceSize>(vertexBufferSize * 1.5f),
                     static_cast<VkDeviceSize>(512 * sizeof(ImDrawVert)));
        frame.vertexBuffer.createVertexBuffer(newCapacity, nullptr);
        updateCmdBuffers = true;
    }

    if (frame.indexBuffer.buffer() == VK_NULL_HANDLE ||
        indexBufferSize > frame.indexBuffer.allocatedSize()) {
        const VkDeviceSize newCapacity =
            std::max(static_cast<VkDeviceSize>(indexBufferSize * 1.5f),
                     static_cast<VkDeviceSize>(1024 * sizeof(ImDrawIdx)));
        frame.indexBuffer.createIndexBuffer(newCapacity, nullptr);
        updateCmdBuffers = true;
    }

    ImDrawVert* vtxDst = static_cast<ImDrawVert*>(frame.vertexBuffer.mapped());
    ImDrawIdx* idxDst = static_cast<ImDrawIdx*>(frame.indexBuffer.mapped());

    for (int n = 0; n < imDrawData->CmdListsCount; ++n) {
        const ImDrawList* cmdList = imDrawData->CmdLists[n];

        // Central opacity fix: ImGui geometry uses vertex alpha for windows, panels,
        // buttons, tabs and text color. Any non-zero UI alpha is promoted to 1.0 here.
        // Glyph edge anti-aliasing is still preserved by the font-atlas alpha sampled
        // in the fragment shader, so text does not turn into rectangular blocks.
        for (int v = 0; v < cmdList->VtxBuffer.Size; ++v) {
            *vtxDst = cmdList->VtxBuffer[v];
            if ((vtxDst->col & IM_COL32_A_MASK) != 0) {
                vtxDst->col |= IM_COL32_A_MASK;
            }
            ++vtxDst;
        }

        std::memcpy(idxDst, cmdList->IdxBuffer.Data,
                    cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
        idxDst += cmdList->IdxBuffer.Size;
    }

    frame.vertexBuffer.flush();
    frame.indexBuffer.flush();
    return updateCmdBuffers;
}

void GuiRenderer::draw(const VkCommandBuffer cmd, VkImageView swapchainImageView,
                       VkViewport viewport, uint32_t frameIndex)
{
    VkRenderingAttachmentInfo swapchainColorAttachment{
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    swapchainColorAttachment.imageView = swapchainImageView;
    swapchainColorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    swapchainColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    swapchainColorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    renderingInfo.renderArea = {0, 0, uint32_t(viewport.width), uint32_t(viewport.height)};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &swapchainColorAttachment;

    ImDrawData* imDrawData = ImGui::GetDrawData();
    if (!imDrawData || imDrawData->CmdListsCount == 0) {
        return;
    }

    auto& frame = *frameData_[frameIndex % frameData_.size()];

    vkCmdBeginRendering(cmd, &renderingInfo);
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    const auto descriptorSet = fontSet_.handle();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, guiPipeline_.pipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            guiPipeline_.pipelineLayout(), 0, 1, &descriptorSet, 0, nullptr);

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

    for (int32_t i = 0; i < imDrawData->CmdListsCount; ++i) {
        const ImDrawList* cmdList = imDrawData->CmdLists[i];
        for (int32_t j = 0; j < cmdList->CmdBuffer.Size; ++j) {
            const ImDrawCmd* drawCmd = &cmdList->CmdBuffer[j];

            VkRect2D scissorRect{};
            scissorRect.offset.x = std::max(int32_t(drawCmd->ClipRect.x), 0);
            scissorRect.offset.y = std::max(int32_t(drawCmd->ClipRect.y), 0);
            scissorRect.extent.width = uint32_t(drawCmd->ClipRect.z - drawCmd->ClipRect.x);
            scissorRect.extent.height = uint32_t(drawCmd->ClipRect.w - drawCmd->ClipRect.y);

            vkCmdSetScissor(cmd, 0, 1, &scissorRect);
            vkCmdDrawIndexed(cmd, drawCmd->ElemCount, 1, indexOffset, vertexOffset, 0);
            indexOffset += drawCmd->ElemCount;
        }
        vertexOffset += cmdList->VtxBuffer.Size;
    }

    vkCmdEndRendering(cmd);
}

void GuiRenderer::resize(uint32_t width, uint32_t height)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(width), float(height));
}

} // namespace hlab
