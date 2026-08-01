#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace {

void check(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                                 std::to_string(result));
    }
}

struct DeviceSelection
{
    VkPhysicalDevice device = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
};

std::optional<DeviceSelection> selectDevice(VkInstance instance, VkSurfaceKHR surface)
{
    std::uint32_t deviceCount = 0;
    check(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr),
          "vkEnumeratePhysicalDevices");
    std::vector<VkPhysicalDevice> devices(deviceCount);
    check(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()),
          "vkEnumeratePhysicalDevices");

    for (VkPhysicalDevice device : devices) {
        std::uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());

        const bool hasSwapchain =
            std::any_of(extensions.begin(), extensions.end(), [](const auto& extension) {
                return std::string(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME;
            });
        if (!hasSwapchain) {
            continue;
        }

        std::uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, queues.data());

        for (std::uint32_t index = 0; index < queueCount; ++index) {
            VkBool32 presentSupported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &presentSupported);
            if ((queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupported) {
                return DeviceSelection{device, index};
            }
        }
    }

    return std::nullopt;
}

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats.front();
}

VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported)
{
    constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> candidates = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (auto candidate : candidates) {
        if (supported & candidate) {
            return candidate;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

} // namespace

int main()
{
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence frameFence = VK_NULL_HANDLE;
    std::vector<VkImageView> imageViews;
    std::vector<VkFramebuffer> framebuffers;

    try {
        glfwSetErrorCallback([](int code, const char* description) {
            std::cerr << "GLFW error " << code << ": " << description << '\n';
        });
        if (!glfwInit()) {
            throw std::runtime_error("glfwInit failed");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        window = glfwCreateWindow(1280, 720, "Vulkan Portfolio - Asset Free Window",
                                  nullptr, nullptr);
        if (!window) {
            throw std::runtime_error("glfwCreateWindow failed");
        }

        std::uint32_t requiredExtensionCount = 0;
        const char** requiredExtensions =
            glfwGetRequiredInstanceExtensions(&requiredExtensionCount);
        if (!requiredExtensions || requiredExtensionCount == 0) {
            throw std::runtime_error("GLFW returned no Vulkan instance extensions");
        }

        VkApplicationInfo applicationInfo{};
        applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        applicationInfo.pApplicationName = "Vulkan Portfolio Basic Window";
        applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        applicationInfo.pEngineName = "None";
        applicationInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        applicationInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &applicationInfo;
        instanceInfo.enabledExtensionCount = requiredExtensionCount;
        instanceInfo.ppEnabledExtensionNames = requiredExtensions;
        check(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");
        check(glfwCreateWindowSurface(instance, window, nullptr, &surface),
              "glfwCreateWindowSurface");

        const auto selection = selectDevice(instance, surface);
        if (!selection) {
            throw std::runtime_error("No GPU supports Vulkan graphics and presentation");
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(selection->device, &properties);
        std::cout << "Rendering with: " << properties.deviceName << '\n';

        const float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = selection->queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;

        const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = deviceExtensions;
        check(vkCreateDevice(selection->device, &deviceInfo, nullptr, &device),
              "vkCreateDevice");

        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(device, selection->queueFamily, 0, &queue);

        VkSurfaceCapabilitiesKHR capabilities{};
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(selection->device, surface, &capabilities),
              "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        std::uint32_t formatCount = 0;
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(selection->device, surface, &formatCount, nullptr),
              "vkGetPhysicalDeviceSurfaceFormatsKHR");
        if (formatCount == 0) {
            throw std::runtime_error("The surface exposes no formats");
        }
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        check(vkGetPhysicalDeviceSurfaceFormatsKHR(
                  selection->device, surface, &formatCount, formats.data()),
              "vkGetPhysicalDeviceSurfaceFormatsKHR");
        const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);

        VkExtent2D extent = capabilities.currentExtent;
        if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
            int width = 0;
            int height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            extent.width = std::clamp(
                static_cast<std::uint32_t>(width),
                capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height = std::clamp(
                static_cast<std::uint32_t>(height),
                capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }

        std::uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0) {
            imageCount = std::min(imageCount, capabilities.maxImageCount);
        }

        VkSwapchainCreateInfoKHR swapchainInfo{};
        swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainInfo.surface = surface;
        swapchainInfo.minImageCount = imageCount;
        swapchainInfo.imageFormat = surfaceFormat.format;
        swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
        swapchainInfo.imageExtent = extent;
        swapchainInfo.imageArrayLayers = 1;
        swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainInfo.preTransform = capabilities.currentTransform;
        swapchainInfo.compositeAlpha =
            chooseCompositeAlpha(capabilities.supportedCompositeAlpha);
        swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapchainInfo.clipped = VK_TRUE;
        check(vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain),
              "vkCreateSwapchainKHR");

        check(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr),
              "vkGetSwapchainImagesKHR");
        std::vector<VkImage> images(imageCount);
        check(vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data()),
              "vkGetSwapchainImagesKHR");

        imageViews.resize(imageCount);
        for (std::uint32_t index = 0; index < imageCount; ++index) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = images[index];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = surfaceFormat.format;
            viewInfo.components = {
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            check(vkCreateImageView(device, &viewInfo, nullptr, &imageViews[index]),
                  "vkCreateImageView");
        }

        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = surfaceFormat.format;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorReference{};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;
        check(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass),
              "vkCreateRenderPass");

        framebuffers.resize(imageCount);
        for (std::uint32_t index = 0; index < imageCount; ++index) {
            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &imageViews[index];
            framebufferInfo.width = extent.width;
            framebufferInfo.height = extent.height;
            framebufferInfo.layers = 1;
            check(vkCreateFramebuffer(
                      device, &framebufferInfo, nullptr, &framebuffers[index]),
                  "vkCreateFramebuffer");
        }

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = selection->queueFamily;
        check(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool),
              "vkCreateCommandPool");

        std::vector<VkCommandBuffer> commandBuffers(imageCount);
        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = imageCount;
        check(vkAllocateCommandBuffers(device, &allocateInfo, commandBuffers.data()),
              "vkAllocateCommandBuffers");

        for (std::uint32_t index = 0; index < imageCount; ++index) {
            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            check(vkBeginCommandBuffer(commandBuffers[index], &beginInfo),
                  "vkBeginCommandBuffer");

            VkClearValue clearColor{};
            clearColor.color = {{0.02f, 0.04f, 0.08f, 1.0f}};

            VkRenderPassBeginInfo passInfo{};
            passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            passInfo.renderPass = renderPass;
            passInfo.framebuffer = framebuffers[index];
            passInfo.renderArea.extent = extent;
            passInfo.clearValueCount = 1;
            passInfo.pClearValues = &clearColor;
            vkCmdBeginRenderPass(commandBuffers[index], &passInfo, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdEndRenderPass(commandBuffers[index]);
            check(vkEndCommandBuffer(commandBuffers[index]), "vkEndCommandBuffer");
        }

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        check(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailable),
              "vkCreateSemaphore");
        check(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinished),
              "vkCreateSemaphore");

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device, &fenceInfo, nullptr, &frameFence), "vkCreateFence");

        std::cout << "Vulkan window is running. Close the window or press Escape.\n";
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
                continue;
            }

            check(vkWaitForFences(device, 1, &frameFence, VK_TRUE, UINT64_MAX),
                  "vkWaitForFences");

            std::uint32_t imageIndex = 0;
            const VkResult acquireResult = vkAcquireNextImageKHR(
                device, swapchain, UINT64_MAX, imageAvailable, VK_NULL_HANDLE, &imageIndex);
            if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
                break;
            }
            if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
                check(acquireResult, "vkAcquireNextImageKHR");
            }

            check(vkResetFences(device, 1, &frameFence), "vkResetFences");

            const VkPipelineStageFlags waitStage =
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &imageAvailable;
            submitInfo.pWaitDstStageMask = &waitStage;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffers[imageIndex];
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &renderFinished;
            check(vkQueueSubmit(queue, 1, &submitInfo, frameFence), "vkQueueSubmit");

            VkPresentInfoKHR presentInfo{};
            presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            presentInfo.waitSemaphoreCount = 1;
            presentInfo.pWaitSemaphores = &renderFinished;
            presentInfo.swapchainCount = 1;
            presentInfo.pSwapchains = &swapchain;
            presentInfo.pImageIndices = &imageIndex;
            const VkResult presentResult = vkQueuePresentKHR(queue, &presentInfo);
            if (presentResult == VK_ERROR_OUT_OF_DATE_KHR) {
                break;
            }
            if (presentResult != VK_SUCCESS && presentResult != VK_SUBOPTIMAL_KHR) {
                check(presentResult, "vkQueuePresentKHR");
            }
        }

        vkDeviceWaitIdle(device);
        vkDestroyFence(device, frameFence, nullptr);
        vkDestroySemaphore(device, renderFinished, nullptr);
        vkDestroySemaphore(device, imageAvailable, nullptr);
        vkDestroyCommandPool(device, commandPool, nullptr);
        for (VkFramebuffer framebuffer : framebuffers) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        vkDestroyRenderPass(device, renderPass, nullptr);
        for (VkImageView view : imageViews) {
            vkDestroyImageView(device, view, nullptr);
        }
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroyDevice(device, nullptr);
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();

        std::cout << "Vulkan window closed successfully.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';

        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
            if (frameFence != VK_NULL_HANDLE) vkDestroyFence(device, frameFence, nullptr);
            if (renderFinished != VK_NULL_HANDLE) vkDestroySemaphore(device, renderFinished, nullptr);
            if (imageAvailable != VK_NULL_HANDLE) vkDestroySemaphore(device, imageAvailable, nullptr);
            if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
            for (VkFramebuffer framebuffer : framebuffers) vkDestroyFramebuffer(device, framebuffer, nullptr);
            if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, renderPass, nullptr);
            for (VkImageView view : imageViews) vkDestroyImageView(device, view, nullptr);
            if (swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, swapchain, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (instance != VK_NULL_HANDLE && surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
        }
        if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
        if (window) glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }
}
