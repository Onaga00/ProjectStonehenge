// Vulkan 1.2 instance, device and swapchain (ADR-003). Loader access through volk; memory through
// VMA. Validation layers are enabled only in development builds and only if installed.
#pragma once

#include <volk.h>

#include <string>
#include <vector>

struct SDL_Window;
struct VmaAllocator_T;
typedef VmaAllocator_T* VmaAllocator;

namespace ac04::render {

struct GpuInfo {
    std::string name;
    std::string driver;
    std::string api_version;
    bool integrated = false;  // VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
};

class VkContext {
public:
    ~VkContext();

    bool Init(SDL_Window* window, bool want_validation, std::string& error);
    void Shutdown();

    // (Re)creates the swapchain for the window's pixel size. `vsync` selects FIFO; otherwise
    // MAILBOX, or IMMEDIATE when MAILBOX is not available.
    bool CreateSwapchain(int width, int height, bool vsync, std::string& error);
    void DestroySwapchain();

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VmaAllocator allocator = nullptr;
    GpuInfo gpu;
    bool dual_src_blend = false;
    // Shared by every pipeline the renderer creates (Renderer::Init; pipeline_cache.h). Internally
    // synchronised: the GS pipeline warm-up threads use it too.
    VkPipelineCache pipeline_cache = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchain_extent{};
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_views;

private:
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
};

const char* VkResultName(VkResult r);

}  // namespace ac04::render
