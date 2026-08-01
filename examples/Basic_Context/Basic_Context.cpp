#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

void printVersion(const char* label, std::uint32_t version)
{
    std::cout << label << VK_API_VERSION_MAJOR(version) << '.'
              << VK_API_VERSION_MINOR(version) << '.'
              << VK_API_VERSION_PATCH(version) << '\n';
}

} // namespace

int main()
{
    std::uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&loaderVersion) != VK_SUCCESS) {
        std::cerr << "Failed to query the Vulkan loader version.\n";
        return 1;
    }
    printVersion("Vulkan loader: ", loaderVersion);

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "Vulkan Portfolio Basic Context";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    applicationInfo.pEngineName = "None";
    applicationInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    applicationInfo.apiVersion = std::min(loaderVersion, VK_API_VERSION_1_3);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &applicationInfo;

    VkInstance instance = VK_NULL_HANDLE;
    const VkResult createResult = vkCreateInstance(&createInfo, nullptr, &instance);
    if (createResult != VK_SUCCESS) {
        std::cerr << "vkCreateInstance failed: " << createResult << '\n';
        return 2;
    }

    std::uint32_t gpuCount = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr);
    if (result != VK_SUCCESS || gpuCount == 0) {
        std::cerr << "No Vulkan-capable GPU was found. Result: " << result << '\n';
        vkDestroyInstance(instance, nullptr);
        return 3;
    }

    std::vector<VkPhysicalDevice> devices(gpuCount);
    result = vkEnumeratePhysicalDevices(instance, &gpuCount, devices.data());
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to enumerate Vulkan GPUs: " << result << '\n';
        vkDestroyInstance(instance, nullptr);
        return 4;
    }

    std::cout << "Detected Vulkan GPUs: " << gpuCount << '\n';
    for (std::uint32_t index = 0; index < gpuCount; ++index) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(devices[index], &properties);
        std::cout << "  [" << index << "] " << properties.deviceName << " (API "
                  << VK_API_VERSION_MAJOR(properties.apiVersion) << '.'
                  << VK_API_VERSION_MINOR(properties.apiVersion) << '.'
                  << VK_API_VERSION_PATCH(properties.apiVersion) << ")\n";
    }

    vkDestroyInstance(instance, nullptr);
    std::cout << "Basic Vulkan context test passed.\n";
    return 0;
}
