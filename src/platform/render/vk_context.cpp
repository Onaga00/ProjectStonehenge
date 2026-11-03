#include "platform/render/vk_context.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vk_mem_alloc.h>

#include <algorithm>
#include <cstring>

#include "platform/log.h"

namespace ac04::render {
namespace {

constexpr uint32_t kRequiredApi = VK_API_VERSION_1_2;

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void* /*user*/) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        AC04_LOG_ERROR("vulkan", "%s", data->pMessage);
    } else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        log::WriteOnce(data->pMessageIdName ? data->pMessageIdName : data->pMessage,
                       log::Level::Warning, "vulkan", "%s", data->pMessage);
    }
    return VK_FALSE;
}

bool HasLayer(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& l : layers)
        if (std::strcmp(l.layerName, name) == 0) return true;
    return false;
}

bool HasInstanceExtension(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());
    for (const auto& e : exts)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

std::string VersionString(uint32_t v) {
    return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) +
           "." + std::to_string(VK_API_VERSION_PATCH(v));
}

}  // namespace

const char* VkResultName(VkResult r) {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        default: return "VK_ERROR_(other)";
    }
}

VkContext::~VkContext() { Shutdown(); }

bool VkContext::Init(SDL_Window* window, bool want_validation, std::string& error) {
    if (volkInitialize() != VK_SUCCESS) {
        error = "No Vulkan driver found (vulkan-1.dll missing). Install or update the GPU driver.";
        return false;
    }
    if (volkGetInstanceVersion() < kRequiredApi) {
        error = "The Vulkan driver supports version " + VersionString(volkGetInstanceVersion()) +
                "; version 1.2 or newer is required. Update the GPU driver.";
        return false;
    }

    Uint32 sdl_ext_count = 0;
    const char* const* sdl_exts = SDL_Vulkan_GetInstanceExtensions(&sdl_ext_count);
    std::vector<const char*> extensions(sdl_exts, sdl_exts + sdl_ext_count);
    std::vector<const char*> layers;
    const bool validation = want_validation && HasLayer("VK_LAYER_KHRONOS_validation") &&
                            HasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (validation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "ProjectStonehenge";
    app.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    app.pEngineName = "ProjectStonehenge";
    app.apiVersion = kRequiredApi;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ici.ppEnabledExtensionNames = extensions.data();
    ici.enabledLayerCount = static_cast<uint32_t>(layers.size());
    ici.ppEnabledLayerNames = layers.data();
    VkResult r = vkCreateInstance(&ici, nullptr, &instance);
    if (r != VK_SUCCESS) {
        error = std::string("vkCreateInstance failed: ") + VkResultName(r);
        return false;
    }
    volkLoadInstanceOnly(instance);
    AC04_LOG_INFO("renderer", "Vulkan instance %s, validation %s",
                  VersionString(volkGetInstanceVersion()).c_str(), validation ? "on" : "off");

    if (validation) {
        VkDebugUtilsMessengerCreateInfoEXT mci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mci.pfnUserCallback = DebugCallback;
        vkCreateDebugUtilsMessengerEXT(instance, &mci, nullptr, &messenger_);
    }

    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
        error = std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError();
        return false;
    }

    // Pick a device: Vulkan 1.2, a queue family with graphics + present, the swapchain extension and
    // VK_EXT_extended_dynamic_state (depth test and depth write set per draw: the GS draws switch
    // them on almost every draw, validation.md §2ao; ADR-009). Discrete GPUs are preferred.
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());
    int best_score = -1;
    for (VkPhysicalDevice pd : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pd, &props);
        if (props.apiVersion < kRequiredApi) continue;
        uint32_t ext_count = 0;
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, nullptr);
        std::vector<VkExtensionProperties> exts(ext_count);
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, exts.data());
        bool has_swapchain = std::any_of(exts.begin(), exts.end(), [](const VkExtensionProperties& e) {
            return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
        });
        if (!has_swapchain) continue;
        const bool has_dynamic_state = std::any_of(exts.begin(), exts.end(), [](const VkExtensionProperties& e) {
            return std::strcmp(e.extensionName, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME) == 0;
        });
        if (!has_dynamic_state) continue;
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        f2.pNext = &eds;
        vkGetPhysicalDeviceFeatures2(pd, &f2);
        if (!eds.extendedDynamicState) continue;
        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, qprops.data());
        for (uint32_t q = 0; q < qcount; ++q) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, q, surface, &present);
            if (!(qprops[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
            const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
            if (score > best_score) {
                best_score = score;
                physical_device = pd;
                queue_family = q;
            }
            break;
        }
    }
    if (!physical_device) {
        error = "No GPU with Vulkan 1.2 and VK_EXT_extended_dynamic_state, graphics and presentation support was found. "
                "Update the GPU driver.";
        return false;
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical_device, &props);
    gpu.name = props.deviceName;
    gpu.api_version = VersionString(props.apiVersion);
    gpu.integrated = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    gpu.driver = std::to_string(props.driverVersion);
    {
        VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        p2.pNext = &driver;
        vkGetPhysicalDeviceProperties2(physical_device, &p2);
        gpu.driver = std::string(driver.driverName) + " " + driver.driverInfo;
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = queue_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    const char* device_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME};
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT};
    eds.extendedDynamicState = VK_TRUE;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &eds;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 2;
    dci.ppEnabledExtensionNames = device_exts;
    // Dual-source blending: the GS blend equation needs the blend factor separate from the alpha
    // written to the frame buffer (src/platform/render/gs/gs_vulkan.cpp).
    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physical_device, &supported);
    VkPhysicalDeviceFeatures enabled{};
    enabled.dualSrcBlend = supported.dualSrcBlend;
    dual_src_blend = supported.dualSrcBlend == VK_TRUE;
    dci.pEnabledFeatures = &enabled;
    r = vkCreateDevice(physical_device, &dci, nullptr, &device);
    if (r != VK_SUCCESS) {
        error = std::string("vkCreateDevice failed: ") + VkResultName(r);
        return false;
    }
    volkLoadDevice(device);
    vkGetDeviceQueue(device, queue_family, 0, &queue);

    VmaVulkanFunctions funcs{};
    funcs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    funcs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo aci{};
    aci.vulkanApiVersion = kRequiredApi;
    aci.physicalDevice = physical_device;
    aci.device = device;
    aci.instance = instance;
    aci.pVulkanFunctions = &funcs;
    r = vmaCreateAllocator(&aci, &allocator);
    if (r != VK_SUCCESS) {
        error = std::string("vmaCreateAllocator failed: ") + VkResultName(r);
        return false;
    }
    return true;
}

bool VkContext::CreateSwapchain(int width, int height, bool vsync, std::string& error) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &caps);

    uint32_t fcount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &fcount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fcount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &fcount, formats.data());
    // UNORM (not sRGB): the PS2 output values go to the screen unchanged.
    VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
    for (const auto& f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }

    uint32_t mcount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mcount, nullptr);
    std::vector<VkPresentModeKHR> modes(mcount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mcount, modes.data());
    auto has = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
    present_mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync) {
        if (has(VK_PRESENT_MODE_MAILBOX_KHR)) present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
        else if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::clamp<uint32_t>(static_cast<uint32_t>(width), caps.minImageExtent.width,
                                            caps.maxImageExtent.width);
        extent.height = std::clamp<uint32_t>(static_cast<uint32_t>(height), caps.minImageExtent.height,
                                             caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) {
        error.clear();  // minimised: no swapchain until the window has a size again
        return false;
    }
    uint32_t image_count = std::max<uint32_t>(caps.minImageCount + 1, 3);
    if (caps.maxImageCount > 0) image_count = std::min(image_count, caps.maxImageCount);

    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = surface;
    sci.minImageCount = image_count;
    sci.imageFormat = chosen.format;
    sci.imageColorSpace = chosen.colorSpace;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = present_mode;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = swapchain;
    VkSwapchainKHR next = VK_NULL_HANDLE;
    VkResult r = vkCreateSwapchainKHR(device, &sci, nullptr, &next);
    DestroySwapchain();
    if (r != VK_SUCCESS) {
        error = std::string("vkCreateSwapchainKHR failed: ") + VkResultName(r);
        return false;
    }
    swapchain = next;
    swapchain_format = chosen.format;
    swapchain_extent = extent;
    AC04_LOG_INFO("renderer", "swapchain %ux%u, %u images, present mode %s", extent.width, extent.height, image_count,
                  present_mode == VK_PRESENT_MODE_FIFO_KHR      ? "FIFO (v-sync)"
                  : present_mode == VK_PRESENT_MODE_MAILBOX_KHR ? "MAILBOX"
                                                                : "IMMEDIATE");

    uint32_t icount = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &icount, nullptr);
    swapchain_images.resize(icount);
    vkGetSwapchainImagesKHR(device, swapchain, &icount, swapchain_images.data());
    swapchain_views.resize(icount);
    for (uint32_t i = 0; i < icount; ++i) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = swapchain_images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = swapchain_format;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device, &vci, nullptr, &swapchain_views[i]);
    }
    return true;
}

void VkContext::DestroySwapchain() {
    if (!device) return;
    for (VkImageView v : swapchain_views) vkDestroyImageView(device, v, nullptr);
    swapchain_views.clear();
    swapchain_images.clear();
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

void VkContext::Shutdown() {
    if (device) vkDeviceWaitIdle(device);
    DestroySwapchain();
    if (allocator) vmaDestroyAllocator(allocator);
    allocator = nullptr;
    if (device) vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
    if (surface) SDL_Vulkan_DestroySurface(instance, surface, nullptr);
    surface = VK_NULL_HANDLE;
    if (messenger_) vkDestroyDebugUtilsMessengerEXT(instance, messenger_, nullptr);
    messenger_ = VK_NULL_HANDLE;
    if (instance) vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
}

}  // namespace ac04::render
