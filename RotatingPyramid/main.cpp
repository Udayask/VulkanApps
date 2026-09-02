#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <vulkan/vulkan.h>
#include <iostream>
#include <string>
#include <vector>
#include <deque>
#include <array>
#include <optional>
#include <functional>
#include <algorithm>
#include <fstream>
#include <map>
#include <chrono>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define APPLICATION_NAME        "SimpleTriangle"
#define WINDOW_WIDTH            1920
#define WINDOW_HEIGHT           1080

/////////////////////////////////////////////////////////////////////////////////////////////

struct Vertex {
    float position[3];
    float color[3];
    float texCoord[2];

    static VkVertexInputBindingDescription GetInputBindingDescription() {
        return {
            0, // binding
            sizeof(Vertex), // stride
            VK_VERTEX_INPUT_RATE_VERTEX // data is per vertex
        };
    }

    static std::array<VkVertexInputAttributeDescription, 3> GetInputAttributeDescriptionArray() {
        std::array<VkVertexInputAttributeDescription, 3> val{};

        val[0] = {
            0, // location
            0, // binding
            VK_FORMAT_R32G32B32_SFLOAT,
            offsetof(Vertex,position)
        };

        val[1] = {
            1, // location
            0, // binding
            VK_FORMAT_R32G32B32_SFLOAT,
            offsetof(Vertex,color)
        };

        val[2] = {
            2, // location
            0, // binding
            VK_FORMAT_R32G32_SFLOAT,
            offsetof(Vertex,texCoord)
        };

        return val;
    }
};

static Vertex vertices[12] = {
    {{ 0.5f,  0.0f, -0.5f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.5f}},
    {{ 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
    {{ 0.5f,  0.0f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},

    {{ 0.5f,  0.0f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}},
    {{ 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
    {{-0.5f,  0.0f,  0.5f}, {0.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},

    {{-0.5f,  0.0f,  0.5f}, {0.0f, 1.0f, 1.0f}, {0.5f, 0.5f}},
    {{ 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},             
    {{-0.5f,  0.0f, -0.5f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},

    {{-0.5f,  0.0f, -0.5f}, {1.0f, 0.0f, 1.0f}, {0.5f, 0.5f}},
    {{ 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},             
    {{ 0.5f,  0.0f, -0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
};

static uint16_t indices[12] = {
    0,  1,  2,
    3,  4,  5,
    6,  7,  8,
    9, 10, 11
};

struct UniformBufferObject {
    glm::mat4 model;
};

struct PushConstant {
    glm::mat4 viewProj;
};

using DeviceUUID = std::array<uint8_t, VK_UUID_SIZE>;

// Renderer's shared color image � export side
struct ExternalImageInfo {
    VkImage        image           = VK_NULL_HANDLE;
    VkDeviceMemory memory          = VK_NULL_HANDLE;
    VkImageView    view            = VK_NULL_HANDLE;
    HANDLE         kmtHandle       = nullptr;   // an NT handle despite the name; CloseHandle() once imported
    VkDeviceSize   allocationSize  = 0;
    uint32_t       memoryTypeIndex = 0;         // must be reused verbatim by the importer (opaque handle rule)
};

// Data Renderer hands to Presenter once, after Renderer fully exports
struct SharedFrameHandles {
    HANDLE            colorMemoryHandle    = nullptr;
    VkDeviceSize      colorMemorySize      = 0;
    uint32_t          colorMemoryTypeIndex = 0;
    VkFormat          colorFormat          = VK_FORMAT_UNDEFINED;
    VkExtent2D        colorExtent          = {};
    VkImageUsageFlags colorUsage           = 0;
    HANDLE            renderReadySemHandle = nullptr;
    HANDLE            copyDoneSemHandle    = nullptr;
};

/////////////////////////////////////////////////////////////////////////////////////////////

class DeletionQueue {
    using fn = std::function<void()>;
    using queue = std::deque<fn>;

    queue dq;

public:
    void Finalize() {
        // delete in reverse order of appends
        for (auto it = dq.rbegin(); it != dq.rend(); it++) {
            (*it)();
        }

        dq.clear();
    }

    template<typename Fn>
    void Append(Fn&& f) {
        dq.emplace_back(f);
    }
};

/////////////////////////////////////////////////////////////////////////////////////////////

#ifdef _DEBUG
static inline const bool enableValidationLayers = true;
#else
static inline const bool enableValidationLayers = false;
#endif

static bool HasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

static std::vector<char> readShaderFile(const std::string& filePath) {
    std::fstream file;
    std::vector<char> fileData;

    file.open(filePath, std::ios::in | std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Could not open shader module to read!");
    }

    std::size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0);

    fileData.resize(fileSize);
    file.read(fileData.data(), fileSize);
    file.close();

    return fileData;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*                                       pUserData)
{
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    }

    return VK_FALSE;
}

// Ladder-style transitions used by both contexts. Cases:
//   UNDEFINED -> TRANSFER_DST_OPTIMAL              (texture upload; Presenter's pre-blit swapchain image)
//   TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL (texture upload)
//   UNDEFINED -> DEPTH_STENCIL_ATTACHMENT_OPTIMAL   (depth image)
//   UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL           (shared image, frame-1 bootstrap only)
//   TRANSFER_DST_OPTIMAL -> PRESENT_SRC_KHR         (Presenter's post-blit swapchain image)
static void TransitionImage(
    VkCommandBuffer     cmdBuffer,
    VkImage             image,
    VkFormat            format,
    VkImageAspectFlags  aspectFlags,
    VkImageLayout       oldLayout,
    VkImageLayout       newLayout)
{
    VkPipelineStageFlags srcStageFlags = 0;
    VkPipelineStageFlags dstStageFlags = 0;

    auto flags = aspectFlags;
    if (HasStencilComponent(format)) {
        flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    VkImageSubresourceRange range { flags, 0, 1, 0, 1 };

    VkImageMemoryBarrier barrier {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        nullptr,
        0,
        0,
        oldLayout,
        newLayout,
        VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED,
        image,
        range,
    };

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStageFlags = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        srcStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = 0;
        srcStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStageFlags = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    }

    vkCmdPipelineBarrier(cmdBuffer, srcStageFlags, dstStageFlags, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// Queue-family-external ownership transfer for the shared color image (always VK_IMAGE_ASPECT_COLOR_BIT).
// Four call sites: B releases, A acquires, A releases, B acquires (see plan �4).
static void TransitionImageQueueFamilyOwnership(
    VkCommandBuffer cmdBuffer,
    VkImage         image,
    VkImageLayout   oldLayout,
    VkImageLayout   newLayout,
    uint32_t        srcQueueFamilyIndex,
    uint32_t        dstQueueFamilyIndex,
    VkAccessFlags   srcAccessMask,
    VkAccessFlags   dstAccessMask,
    VkPipelineStageFlags srcStageFlags,
    VkPipelineStageFlags dstStageFlags)
{
    VkImageSubresourceRange range { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkImageMemoryBarrier barrier {
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        nullptr,
        srcAccessMask,
        dstAccessMask,
        oldLayout,
        newLayout,
        srcQueueFamilyIndex,
        dstQueueFamilyIndex,
        image,
        range,
    };

    vkCmdPipelineBarrier(cmdBuffer, srcStageFlags, dstStageFlags, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

/////////////////////////////////////////////////////////////////////////////////////////////

class alignas(64) Presenter {
public:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

    bool       Init(HINSTANCE hinstance);
    void       ImportSharedResources(const SharedFrameHandles& handles);
    void       PresentFrame(uint64_t frameNumber);
    void       WaitIdle();
    void       Shutdown();
    void       Resize();

    DeviceUUID GetChosenDeviceUUID()     const { return chosenDeviceUUID;     }
    VkFormat   GetSwapChainImageFormat() const { return swapChainImageFormat; }
    VkExtent2D GetSwapChainImageExtent() const { return swapChainImageExtent; }

private:
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;

        bool isComplete() const {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };

    struct SurfaceCaps {
        VkSurfaceCapabilitiesKHR         surfaceCaps;
        std::vector<VkSurfaceFormatKHR>  surfaceFormatVec;
        std::vector<VkPresentModeKHR>    presentModeVec;
    };

    void CreateInstance();
    void OpenWindow(HINSTANCE instance);
    void CreateSurface(HINSTANCE instance);
    void ChoosePhysicalDevice();
    void CreateLogicalDevice();
    void CreateSwapChain();
    void CreateCommandPoolAndBuffers();
    void CreateSyncObjects();
    void OnWindowSizeChanged();
    void RecordBlitCommandBuffer(VkCommandBuffer cmdBuffer, uint32_t imageIndex);

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    using SwapChainImageVec = std::vector<VkImage>;
    using CmdBufferVec      = std::vector<VkCommandBuffer>;
    using SemaphoreVec      = std::vector<VkSemaphore>;
    using FenceVec          = std::vector<VkFence>;

    HWND                     hMainWindow    = NULL;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

    VkInstance               instance       = VK_NULL_HANDLE;
    VkPhysicalDevice         physicalDevice = VK_NULL_HANDLE;
    VkDevice                 device         = VK_NULL_HANDLE;
    VkSurfaceKHR             surface        = VK_NULL_HANDLE;
    VkSwapchainKHR           swapchain      = VK_NULL_HANDLE;

    VkQueue                  graphicsQueue  = VK_NULL_HANDLE;
    VkQueue                  presentQueue   = VK_NULL_HANDLE;
    VkCommandPool            commandPool    = VK_NULL_HANDLE;

    VkBool32                 windowResized  = VK_FALSE;
    uint64_t                 currentFrame   = 0;

    DeletionQueue            deletionQueue;

    CmdBufferVec             cmdBufferVec;
    SemaphoreVec             imageReadyVec;
    SemaphoreVec             renderCompleteVec;
    FenceVec                 gpuBusyVec;
    SwapChainImageVec        swapChainImageVec;

    QueueFamilyIndices       choosenQueueIndices;
    DeviceUUID               chosenDeviceUUID{};

    VkFormat                 swapChainImageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D               swapChainImageExtent{};

    // imported shared color image (Renderer's frame target)
    VkImage                  importedColorImage   = VK_NULL_HANDLE;
    VkDeviceMemory           importedColorMemory  = VK_NULL_HANDLE;
    VkFormat                 sharedColorFormat    = VK_FORMAT_UNDEFINED;
    VkExtent2D               sharedColorExtent{};

    VkSemaphore              renderReadySem = VK_NULL_HANDLE;
    VkSemaphore              copyDoneSem    = VK_NULL_HANDLE;

    PFN_vkImportSemaphoreWin32HandleKHR     vkImportSemaphoreWin32HandleKHR     = VK_NULL_HANDLE;
};

LRESULT CALLBACK Presenter::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
    {
        Presenter* pApp = reinterpret_cast<Presenter*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
        if (pApp) {
            pApp->Resize();
            return 0;
        }
    };
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

bool Presenter::Init(HINSTANCE hinstance) {
    try {
        CreateInstance();
        OpenWindow(hinstance);
        CreateSurface(hinstance);
        ChoosePhysicalDevice();
        CreateLogicalDevice();
        CreateSwapChain();
        CreateCommandPoolAndBuffers();
        CreateSyncObjects();
    }
    catch (std::runtime_error& err) {
        MessageBox(0, err.what(), "Error!", MB_OK);
        std::cerr << err.what() << std::endl;
        return false;
    }
    return true;
}

void Presenter::Resize()   { windowResized = VK_TRUE; }
void Presenter::WaitIdle() { vkDeviceWaitIdle(device); }

void Presenter::Shutdown() {
    vkDeviceWaitIdle(device);
    try {
        deletionQueue.Finalize();
    }
    catch (std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
    }
}

void Presenter::CreateInstance() {
    uint32_t itemCount = 0;
    VkResult result;

    std::vector<const char*> optionalLayers = {
        "VK_LAYER_KHRONOS_validation",
        "VK_LAYER_KHRONOS_synchronization2"
    };

    std::vector<const char*> enabledLayers;

    result = vkEnumerateInstanceLayerProperties(&itemCount, nullptr);
    if (result == VK_SUCCESS && itemCount) {
        std::vector<VkLayerProperties> layPropsVec(itemCount);

        do {
            result = vkEnumerateInstanceLayerProperties(&itemCount, layPropsVec.data());
        } while (result == VK_INCOMPLETE);

        for (auto& r : optionalLayers) {
            for (auto& lay : layPropsVec) {
                if (std::string(lay.layerName) == r) {
                    enabledLayers.emplace_back(r);
                }
            }
        }
    }

    if (enabledLayers.size() != optionalLayers.size()) {
        std::cerr << "Warning! Could not find all requiured layers..." << std::endl;
    }

    std::vector<const char*> requiredExtensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };

    if (enableValidationLayers) {
        requiredExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char*> enabledExtensions;

    result = vkEnumerateInstanceExtensionProperties(nullptr, &itemCount, nullptr);
    if (result == VK_SUCCESS && itemCount) {
        std::vector<VkExtensionProperties> extPropsVec(itemCount);

        do {
            result = vkEnumerateInstanceExtensionProperties(nullptr, &itemCount, extPropsVec.data());
        } while (result == VK_INCOMPLETE);

        for (auto& r : requiredExtensions) {
            for (auto& ext : extPropsVec) {
                if (std::string(ext.extensionName) == r) {
                    enabledExtensions.emplace_back(r);
                }
            }
        }
    }

    if (enabledExtensions.size() != requiredExtensions.size()) {
        throw std::runtime_error("Could not load required extensions!");
    }

    VkApplicationInfo applicationInfo {
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        nullptr,
        "Present POC",
        1,
        "Harmony",
        1,
        VK_MAKE_API_VERSION(0, 1, 3, VK_HEADER_VERSION)
    };

    VkInstanceCreateInfo instanceInfo {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        nullptr,
        0,
        &applicationInfo,
        static_cast<uint32_t>(enabledLayers.size()),
        enabledLayers.data(),
        static_cast<uint32_t>(enabledExtensions.size()),
        enabledExtensions.data()
    };

    if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("Could not create Vk instance!");
    }

    deletionQueue.Append(
        [cinstance = instance] {
            vkDestroyInstance(cinstance, nullptr);
        });

    if (enableValidationLayers) {
        VkDebugUtilsMessengerCreateInfoEXT createInfo{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            nullptr,
            0,
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            DebugCallback,
            nullptr
        };

        auto vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (!vkCreateDebugUtilsMessengerEXT) {
            throw std::runtime_error("Could not get vkCreateDebugUtilsMessengerEXT function address!");
        }

        result = vkCreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("vkCreateDebugUtilsMessengerEXT call failed!");
        }
    }
}

void Presenter::OpenWindow(HINSTANCE hinstance) {
    WNDCLASSEX wcex {
        sizeof(WNDCLASSEX),
        CS_HREDRAW | CS_VREDRAW,
        WndProc,
        0,
        0,
        hinstance,
        LoadIcon(hinstance, IDI_APPLICATION),
        LoadCursor(hinstance, IDC_ARROW),
        (HBRUSH)GetStockObject(BLACK_BRUSH),
        NULL,
        APPLICATION_NAME,
        LoadIcon(hinstance, IDI_APPLICATION)
    };

    if (!RegisterClassEx(&wcex)) {
        throw std::runtime_error("Could not register class!");
    }

    deletionQueue.Append(
        [ cAppName = APPLICATION_NAME
        , cinstance = hinstance ] {
            UnregisterClass(cAppName, cinstance);
        }
    );

    int screenWidth  = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    int windowX = screenWidth / 2 - WINDOW_WIDTH / 2;
    int windowY = screenHeight / 2 - WINDOW_HEIGHT / 2;

    hMainWindow = CreateWindowEx(
        WS_EX_OVERLAPPEDWINDOW,
        APPLICATION_NAME,
        APPLICATION_NAME,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        windowX,
        windowY,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        NULL,
        NULL,
        hinstance,
        NULL);
    if (!hMainWindow) {
        throw std::runtime_error("Could not create main window!");
    }

    ShowWindow(hMainWindow, SW_SHOW);
    UpdateWindow(hMainWindow);
    SetForegroundWindow(hMainWindow);
    SetFocus(hMainWindow);

    deletionQueue.Append(
        [window = hMainWindow] {
            DestroyWindow(window);
        }
    );

    SetWindowLongPtr(hMainWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
}

void Presenter::CreateSurface(HINSTANCE hinstance) {
    VkResult result;

    VkWin32SurfaceCreateInfoKHR createInfo {
        VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        nullptr,
        0,
        hinstance,
        hMainWindow
    };

    result = vkCreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create Win32 surface!");
    }

    deletionQueue.Append(
        [ cinstance = instance
        , csurface  = surface ] {
            vkDestroySurfaceKHR(cinstance, csurface, nullptr);
        }
    );
}

void Presenter::ChoosePhysicalDevice() {
    uint32_t itemCount = 0;
    VkResult result;

    std::vector<const char*> requiredExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
    };

    auto findQueueFamily = [&](VkPhysicalDevice pd) -> QueueFamilyIndices {
        QueueFamilyIndices indices;
        uint32_t qfCount = 0;

        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCount, nullptr);
        if (!qfCount) return indices;

        std::vector<VkQueueFamilyProperties> queueFamilyProps(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCount, queueFamilyProps.data());

        uint32_t i = 0;
        for (auto& qf : queueFamilyProps) {
            VkBool32 presentSupported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &presentSupported);

            if (qf.queueFlags & VK_QUEUE_GRAPHICS_BIT) indices.graphicsFamily = i;
            if (presentSupported) indices.presentFamily = i;
            if (indices.isComplete()) break;
            ++i;
        }
        return indices;
        };

    struct DeviceAndQueueInfo {
        VkPhysicalDevice   physicalDevice;
        QueueFamilyIndices indices;
        DeviceUUID         uuid;
    };

    auto rateDevice = [&](VkPhysicalDevice pd, QueueFamilyIndices& indices, DeviceUUID& outUUID) -> int {
        VkPhysicalDeviceIDProperties idProps { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES, nullptr };
        VkPhysicalDeviceProperties2 deviceProps { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &idProps };

        uint32_t itemCount = 0;
        VkResult result;

        indices = findQueueFamily(pd);
        if (!indices.isComplete()) return 0;

        std::vector<const char*> enabledExtensions;
        result = vkEnumerateDeviceExtensionProperties(pd, nullptr, &itemCount, nullptr);
        if (result == VK_SUCCESS && itemCount) {
            std::vector<VkExtensionProperties> extPropsVec(itemCount);
            do {
                result = vkEnumerateDeviceExtensionProperties(pd, nullptr, &itemCount, extPropsVec.data());
            } while (result == VK_INCOMPLETE);

            for (auto& r : requiredExtensions) {
                for (auto& ext : extPropsVec) {
                    if (std::string(ext.extensionName) == r) enabledExtensions.emplace_back(r);
                }
            }
        }

        if (requiredExtensions.size() != enabledExtensions.size()) return 0;

        int score = 0;
        vkGetPhysicalDeviceProperties2(pd, &deviceProps);
        if (deviceProps.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   score += 1500;
        else if (deviceProps.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 500;

        uint32_t major = VK_API_VERSION_MAJOR(deviceProps.properties.apiVersion);
        uint32_t minor = VK_API_VERSION_MINOR(deviceProps.properties.apiVersion);
        if (major != 1 && minor < 3) return 0;   // preserved verbatim from original (pre-existing && vs || quirk, out of scope)

        memcpy(outUUID.data(), idProps.deviceUUID, VK_UUID_SIZE);
        return score;
        };

    result = vkEnumeratePhysicalDevices(instance, &itemCount, nullptr);
    if (result != VK_SUCCESS || itemCount == 0) {
        throw std::runtime_error("Could not find amy Vulkan capble GPU!");
    }

    std::vector<VkPhysicalDevice>     physDeviceVec(itemCount);
    std::map<int, DeviceAndQueueInfo> deviceMap;

    do {
        result = vkEnumeratePhysicalDevices(instance, &itemCount, physDeviceVec.data());
    } while (result == VK_INCOMPLETE);

    for (auto& pd : physDeviceVec) {
        QueueFamilyIndices indices{};
        DeviceUUID uuid{};
        int score = rateDevice(pd, indices, uuid);
        if (score) deviceMap[score] = { pd, indices, uuid };
    }

    if (deviceMap.empty()) {
        throw std::runtime_error("Could not find suitable device!");
    }

    DeviceAndQueueInfo myDevice = deviceMap.rbegin()->second;
    physicalDevice      = myDevice.physicalDevice;
    choosenQueueIndices = myDevice.indices;
    chosenDeviceUUID    = myDevice.uuid;
}

void Presenter::CreateLogicalDevice() {
    VkResult result;

    std::vector<const char*> requiredExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
    };

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfoVec;

    queueCreateInfoVec.push_back(VkDeviceQueueCreateInfo {
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0,
        choosenQueueIndices.graphicsFamily.value(), 1, &queuePriority });

    if (choosenQueueIndices.presentFamily.value() != choosenQueueIndices.graphicsFamily.value()) {
        queueCreateInfoVec.push_back(VkDeviceQueueCreateInfo {
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0,
            choosenQueueIndices.presentFamily.value(), 1, &queuePriority });
    }

    VkPhysicalDeviceTimelineSemaphoreFeatures timelineSemFeats {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES, nullptr, VK_TRUE };

    VkDeviceCreateInfo deviceCreateInfo {
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &timelineSemFeats, 0,
        static_cast<uint32_t>(queueCreateInfoVec.size()), queueCreateInfoVec.data(),
        0, nullptr,
        static_cast<uint32_t>(requiredExtensions.size()), requiredExtensions.data(),
        nullptr };

    result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not create logical device!");

    deletionQueue.Append([cdevice = device] { vkDestroyDevice(cdevice, nullptr); });

    vkGetDeviceQueue(device, choosenQueueIndices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, choosenQueueIndices.presentFamily.value(), 0, &presentQueue);

    vkImportSemaphoreWin32HandleKHR = (PFN_vkImportSemaphoreWin32HandleKHR)vkGetDeviceProcAddr(device, "vkImportSemaphoreWin32HandleKHR");
}

void Presenter::CreateSwapChain() {
    SurfaceCaps sCaps;
    VkResult    result;
    uint32_t    itemCount = 0;

    // surface caps
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &sCaps.surfaceCaps);

    if (!(sCaps.surfaceCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        throw std::runtime_error("Surface does not support TRANSFER_DST usage for blit target!");
    }

    //
    // grab formats
    //
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &itemCount, nullptr);
    if (result != VK_SUCCESS || itemCount == 0) {
        throw std::runtime_error("failed querying surface formats!");
    }

    sCaps.surfaceFormatVec.resize(itemCount);
    do {
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &itemCount, sCaps.surfaceFormatVec.data());
    } while(result == VK_INCOMPLETE);

    //
    // grab present modes
    //
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &itemCount, nullptr);
    if (result != VK_SUCCESS || itemCount == 0) {
        throw std::runtime_error("failed querying present modes!");
    }

    sCaps.presentModeVec.resize(itemCount);
    do {
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &itemCount, sCaps.presentModeVec.data());
    } while(result == VK_INCOMPLETE);

    // choose a surface format
    VkSurfaceFormatKHR  surfaceFormat = sCaps.surfaceFormatVec[0];
    for (auto& fmt : sCaps.surfaceFormatVec) {
        if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB) {
            surfaceFormat = fmt;
            break;
        }
    }

    // choose present mode
    VkPresentModeKHR    presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto& mode : sCaps.presentModeVec) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            presentMode = mode;
            break;
        }
    }

    // choose extent
    VkExtent2D extent = sCaps.surfaceCaps.currentExtent;
    if( (extent.width == UINT32_MAX) || (extent.height == UINT32_MAX )) {
        extent = { 
            std::clamp<uint32_t>(WINDOW_WIDTH,  sCaps.surfaceCaps.minImageExtent.width,  sCaps.surfaceCaps.maxImageExtent.width),
            std::clamp<uint32_t>(WINDOW_HEIGHT, sCaps.surfaceCaps.minImageExtent.height, sCaps.surfaceCaps.maxImageExtent.height)
        };
    }

    uint32_t numImages = sCaps.surfaceCaps.minImageCount;
    numImages = std::clamp<uint32_t>(numImages, sCaps.surfaceCaps.minImageCount + 1, sCaps.surfaceCaps.maxImageCount);

    VkSharingMode shareMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;
    std::vector<uint32_t>   queueFamilyIndices;

    queueFamilyIndices.push_back(choosenQueueIndices.graphicsFamily.value());

    if (choosenQueueIndices.graphicsFamily != choosenQueueIndices.presentFamily) {
        shareMode = VkSharingMode::VK_SHARING_MODE_CONCURRENT;
        queueFamilyIndices.push_back(choosenQueueIndices.presentFamily.value());
    }

    // swap chain
    VkSwapchainCreateInfoKHR createInfo {
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        nullptr,
        0,
        surface,
        numImages,
        surfaceFormat.format,
        surfaceFormat.colorSpace,
        extent,
        1,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        shareMode,
        static_cast<uint32_t>(queueFamilyIndices.size()),
        queueFamilyIndices.data(),                                          
        sCaps.surfaceCaps.currentTransform,                                 // transform
        VkCompositeAlphaFlagBitsKHR::VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,     // composite alpha
        presentMode,                                                        // presentMode
        VK_TRUE,                                                            // clipped
        VK_NULL_HANDLE,                                                     // old swap chain
    };

    result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create swap chain!");
    }

    deletionQueue.Append(
        [ cdevice = device
        , cswapchain = swapchain ] {
            vkDestroySwapchainKHR(cdevice, cswapchain, nullptr);
        }
    );

    result = vkGetSwapchainImagesKHR(device, swapchain, &numImages, nullptr);
    if (result == VK_SUCCESS && numImages) {
        swapChainImageVec.resize(numImages);

        do {
            result = vkGetSwapchainImagesKHR(device, swapchain, &numImages, swapChainImageVec.data());
        } while( result == VK_INCOMPLETE);
    }

    swapChainImageFormat = surfaceFormat.format;
    swapChainImageExtent = extent;
}

void Presenter::CreateCommandPoolAndBuffers() {
    VkResult result;

    cmdBufferVec.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandPoolCreateInfo cpCreateInfo {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        choosenQueueIndices.graphicsFamily.value() };

    result = vkCreateCommandPool(device, &cpCreateInfo, nullptr, &commandPool);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not create command pool!");

    VkCommandBufferAllocateInfo cbAllocInfo {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr,
        commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, MAX_FRAMES_IN_FLIGHT };

    result = vkAllocateCommandBuffers(device, &cbAllocInfo, cmdBufferVec.data());
    if (result != VK_SUCCESS) throw std::runtime_error("Could not allocate command buffer!");

    deletionQueue.Append([cdevice = device, ccommandPool = commandPool] {
        vkDestroyCommandPool(cdevice, ccommandPool, nullptr);
    });

    // NOTE: no commandPoolTx here � Presenter performs no uploads.
}

void Presenter::CreateSyncObjects() {
    VkResult result;

    imageReadyVec.resize(MAX_FRAMES_IN_FLIGHT);
    renderCompleteVec.resize(MAX_FRAMES_IN_FLIGHT);
    gpuBusyVec.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo smCreateInfo {
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        nullptr,
        0
    };

    VkFenceCreateInfo fnCreateInfo {
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        nullptr,
        VK_FENCE_CREATE_SIGNALED_BIT
    };

    for( uint32_t i =0; i < MAX_FRAMES_IN_FLIGHT; ++i ) {
        result = vkCreateSemaphore(device, &smCreateInfo, nullptr, &imageReadyVec[i]);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Could not create semaphore!");
        }

        result = vkCreateSemaphore(device, &smCreateInfo, nullptr, &renderCompleteVec[i]);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Could not create semaphore!");
        }

        result = vkCreateFence(device, &fnCreateInfo, nullptr, &gpuBusyVec[i]);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Could not create fence!");
        }
    }

    deletionQueue.Append(
        [&] {
            for( uint32_t i =0; i < MAX_FRAMES_IN_FLIGHT; ++i ) {
                vkDestroyFence(device, gpuBusyVec[i], nullptr);
                vkDestroySemaphore(device, renderCompleteVec[i], nullptr);
                vkDestroySemaphore(device, imageReadyVec[i], nullptr);
            }

            gpuBusyVec.clear();
            renderCompleteVec.clear();
            imageReadyVec.clear();
        }
    );
}

void Presenter::ImportSharedResources(const SharedFrameHandles& handles) {
    VkResult result;

    sharedColorFormat = handles.colorFormat;
    sharedColorExtent = handles.colorExtent;

    VkExternalMemoryImageCreateInfo extImageInfo {
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        nullptr,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT
    };

    VkImageCreateInfo imgInfo {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        &extImageInfo,
        0,
        VK_IMAGE_TYPE_2D,
        handles.colorFormat,
        VkExtent3D{ handles.colorExtent.width, handles.colorExtent.height, 1 },
        1, 1,
        VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_TILING_OPTIMAL,
        handles.colorUsage,               // MUST match exporter's usage exactly
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr,
        VK_IMAGE_LAYOUT_UNDEFINED
    };

    result = vkCreateImage(device, &imgInfo, nullptr, &importedColorImage);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not create imported color image!");

    // Opaque handle types (unlike the "typed" D3D11/D3D12 ones) don't support
    // vkGetMemoryWin32HandlePropertiesKHR — memory type indices are a property of the
    // VkPhysicalDevice, so the exporter's index is valid verbatim since both devices share
    // the same physical device (matched by UUID).
    uint32_t typeIndex = handles.colorMemoryTypeIndex;

    VkMemoryDedicatedAllocateInfo dedicatedInfo {
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        nullptr,
        importedColorImage,
        VK_NULL_HANDLE
    };

    VkImportMemoryWin32HandleInfoKHR importInfo {
        VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
        &dedicatedInfo,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
        handles.colorMemoryHandle,
        nullptr
    };

    VkMemoryAllocateInfo allocInfo {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        &importInfo,
        handles.colorMemorySize,           // exporter's reported size, not freshly queried
        typeIndex
    };

    result = vkAllocateMemory(device, &allocInfo, nullptr, &importedColorMemory);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not import shared color image memory!");

    result = vkBindImageMemory(device, importedColorImage, importedColorMemory, 0);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not bind imported color image memory!");

    VkSemaphoreTypeCreateInfo typeInfo {
        VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        nullptr,
        VK_SEMAPHORE_TYPE_TIMELINE,
        0
    };

    VkSemaphoreCreateInfo semCreateInfo {
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        &typeInfo,
        0
    };

    result = vkCreateSemaphore(device, &semCreateInfo, nullptr, &renderReadySem);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not create renderReady semaphore!");

    result = vkCreateSemaphore(device, &semCreateInfo, nullptr, &copyDoneSem);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not create copyDone semaphore!");

    struct ImportPair {
        VkSemaphore sem;
        HANDLE handle;
    };

    ImportPair importPairs[] = {
        { renderReadySem, handles.renderReadySemHandle },
        { copyDoneSem,    handles.copyDoneSemHandle }
    };

    for (auto& pair : importPairs) {
        VkImportSemaphoreWin32HandleInfoKHR importSemInfo {
            VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR, nullptr,
            pair.sem,
            0,      // PERMANENT import (not VK_SEMAPHORE_IMPORT_TEMPORARY_BIT)
            VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT,
            pair.handle, nullptr };

        result = vkImportSemaphoreWin32HandleKHR(device, &importSemInfo);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Could not import semaphore!");
        }
    }

    deletionQueue.Append(
        [cdevice = device, csem1 = renderReadySem, csem2 = copyDoneSem,
        cmemory = importedColorMemory, cimage = importedColorImage] {
            vkDestroySemaphore(cdevice, csem1, nullptr);
            vkDestroySemaphore(cdevice, csem2, nullptr);
            vkFreeMemory(cdevice, cmemory, nullptr);
            vkDestroyImage(cdevice, cimage, nullptr);
        });
}

void Presenter::RecordBlitCommandBuffer(VkCommandBuffer cmdBuffer, uint32_t imageIndex) {
    VkResult result;

    VkCommandBufferBeginInfo beginInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, 0, nullptr };
    result = vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not begin command buffer!");

    // acquire the shared color image from Renderer
    TransitionImageQueueFamilyOwnership(cmdBuffer, importedColorImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_QUEUE_FAMILY_EXTERNAL, choosenQueueIndices.graphicsFamily.value(),
        0, VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    //TransitionImage(cmdBuffer, swapChainImageVec[imageIndex], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT,
    //    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // srcStageMask must match imageReady's wait-dst-stage (TRANSFER_BIT) below, not TOP_OF_PIPE —
    // otherwise this barrier doesn't create a real execution dependency with vkAcquireNextImageKHR's
    // read, and synchronization validation flags a WRITE_AFTER_READ hazard.
    TransitionImageQueueFamilyOwnership(cmdBuffer, swapChainImageVec[imageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageBlit blitRegion {
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { { 0, 0, 0 }, { static_cast<int32_t>(sharedColorExtent.width), static_cast<int32_t>(sharedColorExtent.height), 1 } },
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        { { 0, 0, 0 }, { static_cast<int32_t>(swapChainImageExtent.width), static_cast<int32_t>(swapChainImageExtent.height), 1 } },
    };

    vkCmdBlitImage(cmdBuffer,
        importedColorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapChainImageVec[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blitRegion, VK_FILTER_LINEAR);

    TransitionImage(cmdBuffer, swapChainImageVec[imageIndex], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    // release the shared color image back to Renderer
    TransitionImageQueueFamilyOwnership(cmdBuffer, importedColorImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        choosenQueueIndices.graphicsFamily.value(), VK_QUEUE_FAMILY_EXTERNAL,
        VK_ACCESS_TRANSFER_READ_BIT, 0,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    result = vkEndCommandBuffer(cmdBuffer);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not end command buffer!");
}

void Presenter::OnWindowSizeChanged() {
    vkDeviceWaitIdle(device);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapChainImageVec.clear();
    CreateSwapChain();
    windowResized = VK_FALSE;
}

void Presenter::PresentFrame(uint64_t frameNumber) {
    auto& gpuBusy         = gpuBusyVec[currentFrame];
    auto& imageReady      = imageReadyVec[currentFrame];
    auto& renderComplete  = renderCompleteVec[currentFrame];
    auto& cmdBuffer       = cmdBufferVec[currentFrame];

    vkWaitForFences(device, 1, &gpuBusy, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result;

    for (;;) {
        result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageReady, VK_NULL_HANDLE, &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || windowResized == VK_TRUE) {
            OnWindowSizeChanged();
            continue;   // retry against the freshly recreated swapchain, SAME frameNumber
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) throw std::runtime_error("Could not acquire swapchain image!");
        break;
    }

    vkResetFences(device, 1, &gpuBusy);
    vkResetCommandBuffer(cmdBuffer, 0);
    RecordBlitCommandBuffer(cmdBuffer, imageIndex);

    VkSemaphore          waitSemaphores[]   = { imageReady, renderReadySem };
    uint64_t             waitValues[]       = { 0, frameNumber };            // index 0 (binary) ignored
    VkPipelineStageFlags waitStages[]       = { VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT };
    VkSemaphore          signalSemaphores[] = { renderComplete, copyDoneSem };
    uint64_t             signalValues[]     = { 0, frameNumber };

    VkTimelineSemaphoreSubmitInfo tsInfo {
        VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr,
        2, waitValues, 2, signalValues };

    VkSubmitInfo submitInfo {
        VK_STRUCTURE_TYPE_SUBMIT_INFO, &tsInfo,
        2, waitSemaphores, waitStages,
        1, &cmdBuffer,
        2, signalSemaphores };

    result = vkQueueSubmit(graphicsQueue, 1, &submitInfo, gpuBusy);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not submit cmdbuffer!");

    VkPresentInfoKHR presentInfo {
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr,
        1, &renderComplete, 1, &swapchain, &imageIndex, nullptr };

    result = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || windowResized == VK_TRUE) {
        OnWindowSizeChanged();
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Could not present!");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

/////////////////////////////////////////////////////////////////////////////////////////////

class alignas(64) Renderer {
public:
    bool               Init(const DeviceUUID& targetUUID, VkFormat colorFormat, VkExtent2D colorExtent);
    SharedFrameHandles ExportSharedHandles() const;
    void               ReleaseExportedhandles();
    void               RenderFrame(uint64_t frameNumber);
    void               WaitIdle();
    void               Shutdown();

private:
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> transferFamily;
        bool isComplete() const { return graphicsFamily.has_value() && transferFamily.has_value(); }
    };

    struct BufferInfo {
        VkBuffer       buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void*          cpuVA  = nullptr;
    };

    struct ImageInfo {
        VkImage        image  = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView    view   = VK_NULL_HANDLE;
    };

    void CreateInstance();
    void ChoosePhysicalDevice(const DeviceUUID& targetUUID);
    void CreateLogicalDevice();
    void CreateCommandPoolAndBuffers();
    void CreateSyncObjects();
    void CreateSharedColorImage(VkFormat format, VkExtent2D extent);
    void CreateDepthImageAndView();
    void CreateUniformBuffer();
    void CreateVertexBuffer(VkCommandBuffer cmdBuffer);
    void CreateIndexBuffer(VkCommandBuffer cmdBuffer);
    void CreateTextureImageAndView(VkCommandBuffer cmdBuffer);
    void CreateTextureSampler();
    void CreateDescriptorSetLayout();
    void CreateDescriptorPoolAndSets();
    void CreateGraphicsPipeline();

    void UpdateUbo();
    void RecordCommandBuffer(VkCommandBuffer cmdBuffer, uint64_t frameNumber);

    uint32_t SearchMemoryType(uint32_t typeBits, VkMemoryPropertyFlags mpfFlags);
    BufferInfo CreateBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memPropFlags, VkDeviceSize size);
    void DestroyBuffer(BufferInfo& buffInfo, bool defer = false);
    ImageInfo CreateImage(VkFormat format, VkImageTiling tiling, VkImageUsageFlags usageFlags, VkMemoryPropertyFlags memPropFlags, VkImageAspectFlags aspectFlags, uint32_t width, uint32_t height);
    void DestroyImage(ImageInfo& imgInfo, bool defer = false);
    void CopyBuffer(VkCommandBuffer cmdBuffer, VkBuffer src, VkBuffer dst, VkDeviceSize size);
    void CopyBufferToImage(VkCommandBuffer cmdBuffer, VkBuffer src, VkImage image, uint32_t width, uint32_t height);
    VkCommandBuffer BeginOneTimeCommands();
    void EndOneTimeCommands(VkCommandBuffer cmdBuffer);
    VkImageView CreateImageView(VkImage image, VkFormat imageFormat, VkImageAspectFlags aspectFlags);
    VkFormat findSuitableFormat(const std::vector<VkFormat>& formats, VkImageTiling tiling, VkFormatFeatureFlags features);

    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

    VkInstance       instance       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice         device         = VK_NULL_HANDLE;
    VkQueue          graphicsQueue  = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout      = VK_NULL_HANDLE;
    VkPipeline            graphicsPipeline    = VK_NULL_HANDLE;
    VkCommandPool         commandPool         = VK_NULL_HANDLE;
    VkCommandPool         commandPoolTx       = VK_NULL_HANDLE;
    VkDescriptorPool      descriptorPool      = VK_NULL_HANDLE;
    VkSampler             sampler             = VK_NULL_HANDLE;

    

    VkCommandBuffer  cmdBuffer = VK_NULL_HANDLE;
    VkFence          gpuBusy   = VK_NULL_HANDLE;

    VkSemaphore      renderReadySem       = VK_NULL_HANDLE;
    VkSemaphore      copyDoneSem          = VK_NULL_HANDLE;
    HANDLE           renderReadySemHandle = nullptr;
    HANDLE           copyDoneSemHandle    = nullptr;

    DeletionQueue    deletionQueue;

    BufferInfo       vertexBufferInfo;
    BufferInfo       indexBufferInfo;
    ImageInfo        textureInfo;
    ImageInfo        depthInfo;

    ExternalImageInfo sharedColorInfo;
    VkFormat          sharedColorFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D        sharedColorExtent{};
    VkImageUsageFlags sharedColorUsage  = 0;

    BufferInfo       ubo;
    PushConstant     pushConstant{};
    VkDescriptorSet  descSet = VK_NULL_HANDLE;

    QueueFamilyIndices          choosenQueueIndices;
    VkPhysicalDeviceProperties2 chosenDeviceProps{};
    VkPhysicalDeviceFeatures2   choosenDeviceFeatures{};

    VkFormat depthFormat = VK_FORMAT_UNDEFINED;

    PFN_vkGetPipelineExecutablePropertiesKHR              vkGetPipelineExecutableProperties              = VK_NULL_HANDLE;
    PFN_vkGetPipelineExecutableInternalRepresentationsKHR vkGetPipelineExecutableInternalRepresentations = VK_NULL_HANDLE;
    PFN_vkGetMemoryWin32HandleKHR                         vkGetMemoryWin32HandleKHR                      = VK_NULL_HANDLE;
    PFN_vkGetSemaphoreWin32HandleKHR                      vkGetSemaphoreWin32HandleKHR                   = VK_NULL_HANDLE;
};

bool Renderer::Init(const DeviceUUID& targetUUID, VkFormat colorFormat, VkExtent2D colorExtent) {
    try {
        CreateInstance();
        ChoosePhysicalDevice(targetUUID);
        CreateLogicalDevice();
        CreateCommandPoolAndBuffers();
        CreateSyncObjects();
        CreateSharedColorImage(colorFormat, colorExtent);
        CreateDepthImageAndView();

        auto cmd = BeginOneTimeCommands();
        CreateVertexBuffer(cmd);
        CreateIndexBuffer(cmd);
        CreateTextureImageAndView(cmd);
        EndOneTimeCommands(cmd);

        CreateTextureSampler();
        CreateUniformBuffer();
        CreateDescriptorSetLayout();
        CreateDescriptorPoolAndSets();
        CreateGraphicsPipeline();
    }
    catch (std::runtime_error& err) {
        MessageBox(0, err.what(), "Error!", MB_OK);
        std::cerr << err.what() << std::endl;
        return false;
    }
    return true;
}

void Renderer::WaitIdle() { vkDeviceWaitIdle(device); }

void Renderer::Shutdown() {
    vkDeviceWaitIdle(device);
    try {
        deletionQueue.Finalize();
    }
    catch (std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
    }
}

void Renderer::CreateInstance() {
    uint32_t itemCount = 0;
    VkResult result;

    std::vector<const char*> optionalLayers = {
        "VK_LAYER_KHRONOS_validation",
        "VK_LAYER_KHRONOS_synchronization2"
    };

    std::vector<const char*> enabledLayers;

    result = vkEnumerateInstanceLayerProperties(&itemCount, nullptr);
    if (result == VK_SUCCESS && itemCount) {
        std::vector<VkLayerProperties> layPropsVec(itemCount);

        do {
            result = vkEnumerateInstanceLayerProperties(&itemCount, layPropsVec.data());
        } while (result == VK_INCOMPLETE);

        for (auto& r : optionalLayers) {
            for (auto& lay : layPropsVec) {
                if (std::string(lay.layerName) == r) {
                    enabledLayers.emplace_back(r);
                }
            }
        }
    }

    if (enabledLayers.size() != optionalLayers.size()) {
        std::cerr << "Warning! Could not find all requiured layers..." << std::endl;
    }

    std::vector<const char*> requiredExtensions;

    if (enableValidationLayers) {
        requiredExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    std::vector<const char*> enabledExtensions;

    result = vkEnumerateInstanceExtensionProperties(nullptr, &itemCount, nullptr);
    if (result == VK_SUCCESS && itemCount) {
        std::vector<VkExtensionProperties> extPropsVec(itemCount);

        do {
            result = vkEnumerateInstanceExtensionProperties(nullptr, &itemCount, extPropsVec.data());
        } while (result == VK_INCOMPLETE);

        for (auto& r : requiredExtensions) {
            for (auto& ext : extPropsVec) {
                if (std::string(ext.extensionName) == r) {
                    enabledExtensions.emplace_back(r);
                }
            }
        }
    }

    if (enabledExtensions.size() != requiredExtensions.size()) {
        std::cerr << "Warning! Could not find all requiured extensions..." << std::endl;
    }

    VkApplicationInfo applicationInfo {
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        nullptr,
        "Present POC",
        1,
        "Harmony",
        1,
        VK_MAKE_API_VERSION(0, 1, 3, VK_HEADER_VERSION)
    };

    VkInstanceCreateInfo instanceInfo {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        nullptr,
        0,
        &applicationInfo,
        static_cast<uint32_t>(enabledLayers.size()),
        enabledLayers.data(),
        static_cast<uint32_t>(enabledExtensions.size()),
        enabledExtensions.data()
    };

    if (vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("Could not create Vk instance!");
    }

    deletionQueue.Append(
        [cinstance = instance] {
            vkDestroyInstance(cinstance, nullptr);
        });

    if (enableValidationLayers) {
        VkDebugUtilsMessengerCreateInfoEXT createInfo{
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            nullptr,
            0,
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            DebugCallback,
            nullptr
        };

        auto vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (!vkCreateDebugUtilsMessengerEXT) {
            throw std::runtime_error("Could not get vkCreateDebugUtilsMessengerEXT function address!");
        }

        result = vkCreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("vkCreateDebugUtilsMessengerEXT call failed!");
        }
    }
}

void Renderer::ChoosePhysicalDevice(const DeviceUUID& targetUUID) {
    uint32_t itemCount = 0;
    VkResult result;

    std::vector<const char*> requiredExtensions = {
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
    };

    result = vkEnumeratePhysicalDevices(instance, &itemCount, nullptr);
    if (result != VK_SUCCESS || itemCount == 0) throw std::runtime_error("Could not find amy Vulkan capble GPU!");

    std::vector<VkPhysicalDevice> physDeviceVec(itemCount);
    do {
        result = vkEnumeratePhysicalDevices(instance, &itemCount, physDeviceVec.data());
    } while (result == VK_INCOMPLETE);

    for (auto& pd : physDeviceVec) {
        VkPhysicalDeviceIDProperties idProps {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES,
            nullptr
        };

        VkPhysicalDeviceProperties2  props2  {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            &idProps
        };

        vkGetPhysicalDeviceProperties2(pd, &props2);

        if (memcmp(idProps.deviceUUID, targetUUID.data(), VK_UUID_SIZE) == 0) {
            physicalDevice = pd;
            break;
        }
    }

    if (physicalDevice == VK_NULL_HANDLE) throw std::runtime_error("No physical device in Renderer's VkInstance matches Presenter's chosen GPU!");

    std::vector<const char*> enabledExtensions;
    result = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &itemCount, nullptr);
    if (result == VK_SUCCESS && itemCount) {
        std::vector<VkExtensionProperties> extPropsVec(itemCount);
        do {
            result = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &itemCount, extPropsVec.data());
        } while (result == VK_INCOMPLETE);

        for (auto& r : requiredExtensions) {
            for (auto& ext : extPropsVec) {
                if (std::string(ext.extensionName) == r) enabledExtensions.emplace_back(r);
            }
        }
    }

    if (requiredExtensions.size() != enabledExtensions.size()) throw std::runtime_error("Matched physical device does not support all required extensions!");

    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilyProps(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, queueFamilyProps.data());

    uint32_t i = 0;
    for (auto& qf : queueFamilyProps) {
        if (qf.queueFlags & VK_QUEUE_GRAPHICS_BIT) choosenQueueIndices.graphicsFamily = i;
        if (qf.queueFlags & VK_QUEUE_TRANSFER_BIT) choosenQueueIndices.transferFamily = i;
        if (choosenQueueIndices.isComplete()) break;
        ++i;
    }
    if (!choosenQueueIndices.isComplete()) throw std::runtime_error("Matched physical device has no suitable queue families!");

    chosenDeviceProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    vkGetPhysicalDeviceProperties2(physicalDevice, &chosenDeviceProps);

    choosenDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &choosenDeviceFeatures);
}

void Renderer::CreateLogicalDevice() {
    VkResult result;

    std::vector<const char*> requiredExtensions = {
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
    };

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfoVec;
    queueCreateInfoVec.push_back(VkDeviceQueueCreateInfo {
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0,
        choosenQueueIndices.graphicsFamily.value(), 1, &queuePriority });

    VkPhysicalDeviceTimelineSemaphoreFeatures timelineSemFeats {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        nullptr,
        VK_TRUE
    };

    VkPhysicalDeviceDynamicRenderingFeatures dynRenderingFeats {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        &timelineSemFeats,
        VK_TRUE
    };

    VkDeviceCreateInfo deviceCreateInfo {
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        &dynRenderingFeats,
        0,
        static_cast<uint32_t>(queueCreateInfoVec.size()), queueCreateInfoVec.data(),
        0, nullptr,
        static_cast<uint32_t>(requiredExtensions.size()), requiredExtensions.data(),
        &choosenDeviceFeatures.features };

    result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not create logical device!");

    deletionQueue.Append([cdevice = device] { vkDestroyDevice(cdevice, nullptr); });

    vkGetDeviceQueue(device, choosenQueueIndices.graphicsFamily.value(), 0, &graphicsQueue);

    vkGetMemoryWin32HandleKHR = (PFN_vkGetMemoryWin32HandleKHR)vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandleKHR");
    vkGetSemaphoreWin32HandleKHR = (PFN_vkGetSemaphoreWin32HandleKHR)vkGetDeviceProcAddr(device, "vkGetSemaphoreWin32HandleKHR");
}

void Renderer::CreateCommandPoolAndBuffers() {
    VkResult result;

    // graphics command pool & command buffers
    VkCommandPoolCreateInfo cpCreateInfo {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        nullptr,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        choosenQueueIndices.graphicsFamily.value()
    };

    result = vkCreateCommandPool(device, &cpCreateInfo, nullptr, &commandPool);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create command pool!");
    }

    VkCommandBufferAllocateInfo cbAllocInfo {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        nullptr,
        commandPool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        1
    };

    result = vkAllocateCommandBuffers(device, &cbAllocInfo, &cmdBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not allocate command buffer!");
    }

    deletionQueue.Append(
        [ cdevice = device
        , ccommandPool = commandPool ] {
            // cmd buffers are freed when cmd pool is destroyed
            vkDestroyCommandPool(cdevice, ccommandPool, nullptr);
        }
    );

    // transfer command pool 
    cpCreateInfo.queueFamilyIndex = choosenQueueIndices.transferFamily.value();
    result = vkCreateCommandPool(device, &cpCreateInfo, nullptr, &commandPoolTx);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create transfer command pool!");
    }

    deletionQueue.Append(
        [ cdevice = device
        , ccommandPool = commandPoolTx ] {
            // cmd buffers are freed when cmd pool is destroyed
            vkDestroyCommandPool(cdevice, ccommandPool, nullptr);
        }
    );
}

void Renderer::CreateSyncObjects() {
    VkResult result;

    VkFenceCreateInfo fnCreateInfo { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT };
    result = vkCreateFence(device, &fnCreateInfo, nullptr, &gpuBusy);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not create fence!");

    VkExportSemaphoreCreateInfo exportInfo {
        VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
        nullptr,
        VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT
    };

    VkSemaphoreTypeCreateInfo typeInfo {
        VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        &exportInfo,
        VK_SEMAPHORE_TYPE_TIMELINE,
        0
    };

    VkSemaphoreCreateInfo semCreateInfo {
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        &typeInfo,
        0
    };

    result = vkCreateSemaphore(device, &semCreateInfo, nullptr, &renderReadySem);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not create renderReady semaphore!");

    result = vkCreateSemaphore(device, &semCreateInfo, nullptr, &copyDoneSem);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not create copyDone semaphore!");

    VkSemaphoreGetWin32HandleInfoKHR getRenderReadyInfo {
        VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR,
        nullptr,
        renderReadySem,
        VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT
    };

    result = vkGetSemaphoreWin32HandleKHR(device, &getRenderReadyInfo, &renderReadySemHandle);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not export renderReady semaphore handle!");
    }

    VkSemaphoreGetWin32HandleInfoKHR getCopyDoneInfo {
        VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR,
        nullptr,
        copyDoneSem,
        VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT
    };

    result = vkGetSemaphoreWin32HandleKHR(device, &getCopyDoneInfo, &copyDoneSemHandle);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not export copyDone semaphore handle!");
    }

    deletionQueue.Append([cdevice = device, cfence = gpuBusy, csem1 = renderReadySem, csem2 = copyDoneSem] {
        vkDestroyFence(cdevice, cfence, nullptr);
        vkDestroySemaphore(cdevice, csem2, nullptr);
        vkDestroySemaphore(cdevice, csem1, nullptr);
    });
}

void Renderer::CreateSharedColorImage(VkFormat format, VkExtent2D extent) {
    VkResult result;

    VkExternalMemoryImageCreateInfo extImageInfo {
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO, nullptr,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT };

    VkImageCreateInfo createInfo {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &extImageInfo, 0,
        VK_IMAGE_TYPE_2D, format, VkExtent3D{ extent.width, extent.height, 1 },
        1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, nullptr, VK_IMAGE_LAYOUT_UNDEFINED };

    result = vkCreateImage(device, &createInfo, nullptr, &sharedColorInfo.image);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not create shared color image!");

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(device, sharedColorInfo.image, &memReq);
    sharedColorInfo.allocationSize = memReq.size;

    uint32_t typeIndex = SearchMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    sharedColorInfo.memoryTypeIndex = typeIndex;   // exporter's index, reused verbatim by Presenter's import

    VkMemoryDedicatedAllocateInfo dedicatedInfo {
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, nullptr, sharedColorInfo.image, VK_NULL_HANDLE };
    VkExportMemoryAllocateInfo exportInfo {
        VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, &dedicatedInfo,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT };
    VkMemoryAllocateInfo allocInfo {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &exportInfo, memReq.size, typeIndex };

    result = vkAllocateMemory(device, &allocInfo, nullptr, &sharedColorInfo.memory);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not allocate shared color image memory!");

    result = vkBindImageMemory(device, sharedColorInfo.image, sharedColorInfo.memory, 0);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not bind shared color image memory!");

    VkMemoryGetWin32HandleInfoKHR getHandleInfo {
        VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR, nullptr,
        sharedColorInfo.memory, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT };
    result = vkGetMemoryWin32HandleKHR(device, &getHandleInfo, &sharedColorInfo.kmtHandle);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not export shared color image memory handle!");

    sharedColorInfo.view = CreateImageView(sharedColorInfo.image, format, VK_IMAGE_ASPECT_COLOR_BIT);

    sharedColorFormat = format;
    sharedColorExtent = extent;
    sharedColorUsage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    deletionQueue.Append([cdevice = device, cinfo = sharedColorInfo] {
        vkDestroyImageView(cdevice, cinfo.view, nullptr);
        vkFreeMemory(cdevice, cinfo.memory, nullptr);
        vkDestroyImage(cdevice, cinfo.image, nullptr);
        });
}

void Renderer::CreateDepthImageAndView() {
    depthFormat = findSuitableFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

    depthInfo = CreateImage(depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
        sharedColorExtent.width, sharedColorExtent.height);   // <- was swapChainImageExtent
    DestroyImage(depthInfo, true);

    auto cmd = BeginOneTimeCommands();
    TransitionImage(cmd, depthInfo.image, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    EndOneTimeCommands(cmd);
}

void Renderer::CreateUniformBuffer() {
    VkDeviceSize uboSize = sizeof(glm::mat4);
    ubo = CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uboSize);
    vkMapMemory(device, ubo.memory, 0, uboSize, 0, &ubo.cpuVA);
    DestroyBuffer(ubo, true);
}

void Renderer::CreateVertexBuffer(VkCommandBuffer cmdBuffer) {
    VkDeviceSize size  = sizeof vertices;

    vertexBufferInfo  = CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        size);

    // destroy when app exits
    DestroyBuffer(vertexBufferInfo, true);

    auto stagingBufferInfo = CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        size);

    {
        void *pdata = nullptr;

        VkResult result = vkMapMemory(device, stagingBufferInfo.memory, 0, size, 0, &pdata);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Could not map memory!");
        }

        memcpy_s(pdata, size, vertices, size);

        vkUnmapMemory(device, stagingBufferInfo.memory);
    }

    CopyBuffer(cmdBuffer, stagingBufferInfo.buffer, vertexBufferInfo.buffer, size);

    DestroyBuffer(stagingBufferInfo, true);
}

void Renderer::CreateIndexBuffer(VkCommandBuffer cmdBuffer) {
    VkDeviceSize size = sizeof(uint16_t) * 12;

    indexBufferInfo = CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        size);

    // destroy when app exits
    DestroyBuffer(indexBufferInfo, true);

    auto stagingBufferInfo = CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_CACHED_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        size);

    {
        void *pdata = nullptr;

        VkResult result = vkMapMemory(device, stagingBufferInfo.memory, 0, size, 0, &pdata);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Could not map memory!");
        }

        memcpy_s(pdata, size, indices, size);

        vkUnmapMemory(device, stagingBufferInfo.memory);
    }

    CopyBuffer(cmdBuffer, stagingBufferInfo.buffer, indexBufferInfo.buffer, size);

    DestroyBuffer(stagingBufferInfo, true);
}

void Renderer::CreateTextureImageAndView(VkCommandBuffer cmdBuffer) {
    int texWidth, texHeight, texChannels;
    VkResult result;

    stbi_uc* pPixels = stbi_load("textures/checkerboard.png", &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pPixels) {
        throw std::runtime_error("Could noit load texture!");
    }

    VkDeviceSize imageSize = texWidth * texHeight * 4; // RGBA 

    auto stagingBuffer = CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, imageSize);

    void *stagingPtr = nullptr;
    result = vkMapMemory(device, stagingBuffer.memory, 0, imageSize, 0, &stagingPtr);
    if (result == VK_SUCCESS) {
        memcpy_s(stagingPtr, imageSize, pPixels, imageSize);
        vkUnmapMemory(device, stagingBuffer.memory);
    }
    else {
        throw std::runtime_error("Could not map staging memory!");
    }

    stbi_image_free(pPixels);

    textureInfo = CreateImage(VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_COLOR_BIT, texWidth, texHeight); 
    DestroyImage(textureInfo, true);

    TransitionImage(cmdBuffer, textureInfo.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CopyBufferToImage(cmdBuffer, stagingBuffer.buffer, textureInfo.image, texWidth, texHeight);
    TransitionImage(cmdBuffer, textureInfo.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    DestroyBuffer(stagingBuffer, true);
}

void Renderer::CreateTextureSampler() {
    VkResult result;

    VkSamplerCreateInfo createInfo {
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        nullptr,
        0, // flags
        VK_FILTER_LINEAR,
        VK_FILTER_LINEAR,
        VK_SAMPLER_MIPMAP_MODE_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        0.0f,
        VK_TRUE,
        chosenDeviceProps.properties.limits.maxSamplerAnisotropy,
        VK_FALSE,
        VK_COMPARE_OP_ALWAYS,
        0.0f,
        0.0f,
        VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        VK_FALSE
    };

    result = vkCreateSampler(device, &createInfo, nullptr, &sampler);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create sampler object!");
    }

    deletionQueue.Append(
        [cdevice = device
        , csampler = sampler] {
            vkDestroySampler(cdevice, csampler, nullptr);
        }
    );
}

void Renderer::CreateDescriptorPoolAndSets() {
    VkResult result;

    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }
        }};

    VkDescriptorPoolCreateInfo createInfo {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 2, poolSizes.data() };

    result = vkCreateDescriptorPool(device, &createInfo, nullptr, &descriptorPool);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not create descriptor pool!");

    deletionQueue.Append([cdevice = device, cpool = descriptorPool] {
        vkDestroyDescriptorPool(cdevice, cpool, nullptr);
        });

    VkDescriptorSetAllocateInfo allocInfo {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, descriptorPool, 1, &descriptorSetLayout };
    result = vkAllocateDescriptorSets(device, &allocInfo, &descSet);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not allocate descriptor sets!");

    VkDescriptorBufferInfo buffInfo { ubo.buffer, 0, VK_WHOLE_SIZE };
    VkDescriptorImageInfo imageInfo { sampler, textureInfo.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

    std::array<VkWriteDescriptorSet, 2> writeDescs = {{
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descSet, 0, 0, 1,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffInfo, nullptr },
        { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descSet, 1, 0, 1,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfo, nullptr, nullptr }
        }};

    vkUpdateDescriptorSets(device, 2, writeDescs.data(), 0, nullptr);
}

void Renderer::CreateDescriptorSetLayout() {
    VkResult result;

    VkDescriptorSetLayoutBinding uboLayoutBinding {
        0,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        1,
        VK_SHADER_STAGE_ALL,
        nullptr
    };

    VkDescriptorSetLayoutBinding samplerLayoutBinding {
        1,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_ALL,
        nullptr
    };

    std::array<VkDescriptorSetLayoutBinding, 2> layoutBinding = { uboLayoutBinding, samplerLayoutBinding };

    VkDescriptorSetLayoutCreateInfo dsCreateInfo {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        nullptr,
        0,
        2,
        layoutBinding.data()
    };

    result = vkCreateDescriptorSetLayout(device, &dsCreateInfo, nullptr, &descriptorSetLayout);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create descriptor set layout");
    }

    deletionQueue.Append(
        [ cdevice = device,
        clayout = descriptorSetLayout ]
        {
            vkDestroyDescriptorSetLayout(cdevice , clayout, nullptr);
        }
    );
}

void Renderer::CreateGraphicsPipeline() {
    CHAR currentDirectory[MAX_PATH + 1];
    VkResult result;

    GetCurrentDirectory(MAX_PATH, currentDirectory);

    std::string vPath(currentDirectory);
    vPath += "\\shaders\\shader.vert.spv";
    auto vShader = readShaderFile(vPath);

    std::string fPath(currentDirectory);
    fPath += "\\shaders\\shader.frag.spv";
    auto fShader = readShaderFile(fPath);

    VkShaderModule vShaderModule, fShaderModule;

    {
        VkShaderModuleCreateInfo vCreateInfo {
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            nullptr,
            0,
            static_cast<uint32_t>(vShader.size()),
            reinterpret_cast<uint32_t*>(vShader.data())
        };

        result = vkCreateShaderModule(device, &vCreateInfo, nullptr, &vShaderModule);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Could not create vertex shader module!");
        }
    }

    {
        VkShaderModuleCreateInfo fCreateInfo {
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            nullptr,
            0,
            static_cast<uint32_t>(fShader.size()),
            reinterpret_cast<uint32_t*>(fShader.data())
        };

        result = vkCreateShaderModule(device, &fCreateInfo, nullptr, &fShaderModule);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Could not create fragment shader module!");
        }
    }

    VkPipelineShaderStageCreateInfo vShaderStageCreateInfo {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        nullptr,
        0,
        VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT,
        vShaderModule,
        "main",
        nullptr    // no specialization constants
    };

    VkPipelineShaderStageCreateInfo fShaderStageCreateInfo {
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        nullptr,
        0,
        VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT,
        fShaderModule,
        "main",
        nullptr    // no specialization constants
    };

    VkPipelineShaderStageCreateInfo shaderStagesCreateInfos[] = { vShaderStageCreateInfo, fShaderStageCreateInfo };

    // dynamic states
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynStateCreateInfo {
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        nullptr,
        0,
        static_cast<uint32_t>(dynamicStates.size()),
        dynamicStates.data()
    };

    VkPipelineViewportStateCreateInfo vpStateCreateInfo {
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        nullptr,
        0,
        1, // viewportCount
        nullptr, // specified in cmd buffer
        1, // scissorCount
        nullptr  // specified in cmd buffer
    };

    auto vertexBindingDescription = Vertex::GetInputBindingDescription();
    auto vertexAttributeDescription = Vertex::GetInputAttributeDescriptionArray();

    VkPipelineVertexInputStateCreateInfo vfStateCreateInfo {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        nullptr,
        0,
        1,  // vertexBindingDescriptionCount
        &vertexBindingDescription,
        static_cast<uint32_t>(vertexAttributeDescription.size()),  // vertexAttributeDescriptionCount
        vertexAttributeDescription.data()
    };

    VkPipelineInputAssemblyStateCreateInfo iaStateCreateInfo {
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        nullptr,
        0,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        VK_FALSE // primitiveRestartEnable
    };

    VkPipelineRasterizationStateCreateInfo rsStateCreateInfo {
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        nullptr,
        0,
        VK_FALSE, // depthClampEnable
        VK_FALSE, // rasterizerDiscardEnable - we render to RT
        VkPolygonMode::VK_POLYGON_MODE_FILL,
        VK_CULL_MODE_BACK_BIT,
        VK_FRONT_FACE_COUNTER_CLOCKWISE,
        VK_FALSE, // depthBiasEnable
        0.0f,     // depthBiasConstantFactor
        0.0f,     // depthBiasClamp
        0.0f,     // depthBiasSlopeFactor
        0.0f      // lineWidth
    };

    VkPipelineMultisampleStateCreateInfo msStateCreateInfo {
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        nullptr,
        0,
        VK_SAMPLE_COUNT_1_BIT ,
        VK_FALSE,
        0.0f,
        nullptr,
        VK_FALSE,
        VK_FALSE
    };

    VkPipelineDepthStencilStateCreateInfo dsStateCreateInfo {
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        nullptr,
        0,
        VK_TRUE,              // depth test enable
        VK_TRUE,              // depth write enable
        VK_COMPARE_OP_LESS,
        VK_FALSE,             // depth bounds test
        VK_FALSE,             // stencil
        {},                   // front 
        {},                   // & back stencil op
        0.0f,                 // min depth bound
        1.0f                  // max depth bound
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachmentState {
        VK_FALSE, // blendEnable
        VK_BLEND_FACTOR_ONE,  // srcColorBlendFactor
        VK_BLEND_FACTOR_ZERO, // dstColorBlendFactor
        VK_BLEND_OP_ADD,      // colorBlendOp
        VK_BLEND_FACTOR_ONE,  // srcAlphaBlendFactor
        VK_BLEND_FACTOR_ZERO, // dstAlphaBlendFactor
        VK_BLEND_OP_ADD,      // alphaBlendOp
        VK_COLOR_COMPONENT_A_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_R_BIT  // colorWriteMask
    };

    VkPipelineColorBlendStateCreateInfo cbStateCreateInfo {
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        nullptr,
        0,
        VK_FALSE,           // logicOpEnable
        VK_LOGIC_OP_COPY,   // op if enabled
        1,                  // attachment count
        &colorBlendAttachmentState,
        { 0.0f, 0.0f, 0.0f, 0.0f }  // blend constants
    };

    VkPushConstantRange pushConstantRange {
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(PushConstant)
    };

    VkPipelineLayoutCreateInfo plCreateInfo {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        nullptr,
        0,
        1,                    // setLayoutCOunt
        &descriptorSetLayout, // pSetLayouts
        1,                    // pushConstantRangeCount
        &pushConstantRange,   // pPushConstantRanges
    };

    result = vkCreatePipelineLayout(device, &plCreateInfo, nullptr, &pipelineLayout);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create pipeline layout!");
    }

    deletionQueue.Append(
        [ cdevice = device
        , cpl = pipelineLayout ] {
            vkDestroyPipelineLayout(cdevice, cpl, nullptr);
        }
    );

    VkPipelineCreateFlags plFlags = 0;

    VkPipelineRenderingCreateInfo plRenderingInfo {
        VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        nullptr,
        0,
        1,
        &sharedColorFormat,
        depthFormat,
        VK_FORMAT_UNDEFINED
    };

    if (HasStencilComponent(depthFormat) ) {
        plRenderingInfo.stencilAttachmentFormat = depthFormat;
    }

    VkGraphicsPipelineCreateInfo pipelineCreateInfo {
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        &plRenderingInfo,
        plFlags,
        2,
        shaderStagesCreateInfos,
        &vfStateCreateInfo,
        &iaStateCreateInfo,
        nullptr,
        &vpStateCreateInfo,
        &rsStateCreateInfo,
        &msStateCreateInfo,
        &dsStateCreateInfo,
        &cbStateCreateInfo,
        &dynStateCreateInfo,
        pipelineLayout,
        VK_NULL_HANDLE,
        0,
        VK_NULL_HANDLE,
        -1
    };

    result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &graphicsPipeline);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create graphics pipeline!");
    }

    deletionQueue.Append(
        [ cdevice = device
        , cpipeline = graphicsPipeline ] {
            vkDestroyPipeline(cdevice, cpipeline, nullptr);
        }
    );

    vkDestroyShaderModule(device, fShaderModule, nullptr);
    vkDestroyShaderModule(device, vShaderModule, nullptr);
}

void Renderer::UpdateUbo() {
    static auto epoch = std::chrono::high_resolution_clock::now();
    auto current = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(current - epoch).count();

    auto model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    float yDisplacement = (glm::sin(time * 5) * 0.25f) - 0.25f;
    model = glm::translate(model, glm::vec3(0.0f, yDisplacement, 0.0f));

    auto view = glm::lookAt(glm::vec3(0.0f, 0.25f, -1.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    auto proj = glm::perspective(glm::radians(70.0f),
        float(sharedColorExtent.width) / sharedColorExtent.height, 0.1f, 20.0f);   // <- was swapChainImageExtent

    const glm::mat4 clip = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0, -1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.5f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    pushConstant = { clip * proj * view };
    memcpy_s(ubo.cpuVA, sizeof(model), &model, sizeof(model));
}

void Renderer::RecordCommandBuffer(VkCommandBuffer cmdBuffer, uint64_t frameNumber) {
    VkResult result;

    VkCommandBufferBeginInfo beginInfo { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, nullptr, 0, nullptr };
    result = vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not begin command buffer!");

    VkClearValue clearValue[2];
    clearValue[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
    clearValue[1].depthStencil = {1.0f, 0};

    VkRenderingAttachmentInfo colorAttachmentInfo {
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr,
        sharedColorInfo.view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_RESOLVE_MODE_NONE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, clearValue[0] };

    VkRenderingAttachmentInfo depthAttachmentInfo {
        VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr,
        depthInfo.view, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_RESOLVE_MODE_NONE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE, clearValue[1] };

    VkRenderingInfo renderInfo {
        VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0,
        VkRect2D{ VkOffset2D{}, sharedColorExtent },
        1, 0, 1, &colorAttachmentInfo, &depthAttachmentInfo, nullptr };

    if (HasStencilComponent(depthFormat)) renderInfo.pStencilAttachment = &depthAttachmentInfo;

    VkViewport vp { 0.0f, 0.0f, static_cast<float>(sharedColorExtent.width), static_cast<float>(sharedColorExtent.height), 0.0f, 1.0f };
    VkRect2D scissor { 0, 0, sharedColorExtent.width, sharedColorExtent.height };

    if (frameNumber == 1) {
        // Bootstrap: nobody has ever released this image to EXTERNAL yet.
        TransitionImage(cmdBuffer, sharedColorInfo.image, sharedColorFormat, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    } else {
        // Acquire from Presenter, which released it last frame.
        TransitionImageQueueFamilyOwnership(cmdBuffer, sharedColorInfo.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_QUEUE_FAMILY_EXTERNAL, choosenQueueIndices.graphicsFamily.value(),
            0, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    }

    TransitionImage(cmdBuffer, depthInfo.image, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    vkCmdBeginRendering(cmdBuffer, &renderInfo);
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    VkBuffer vbs[] = { vertexBufferInfo.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vbs, offsets);
    vkCmdBindIndexBuffer(cmdBuffer, indexBufferInfo.buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdSetViewport(cmdBuffer, 0, 1, &vp);
    vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descSet, 0, nullptr);
    vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstant), &pushConstant);
    vkCmdDrawIndexed(cmdBuffer, 12, 1, 0, 0, 0);
    vkCmdEndRendering(cmdBuffer);

    // Release to Presenter � every frame, including frame 1.
    TransitionImageQueueFamilyOwnership(cmdBuffer, sharedColorInfo.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        choosenQueueIndices.graphicsFamily.value(), VK_QUEUE_FAMILY_EXTERNAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

    result = vkEndCommandBuffer(cmdBuffer);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not end command buffer!");
}

void Renderer::RenderFrame(uint64_t frameNumber) {
    VkResult result;

    vkWaitForFences(device, 1, &gpuBusy, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &gpuBusy);
    vkResetCommandBuffer(cmdBuffer, 0);

    UpdateUbo();
    RecordCommandBuffer(cmdBuffer, frameNumber);

    VkSemaphore          waitSemaphores[]   = { copyDoneSem };
    uint64_t             waitValues[]       = { frameNumber - 1 };   // frame 1 waits on value 0, already satisfied
    VkPipelineStageFlags waitStages[]       = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore          signalSemaphores[] = { renderReadySem };
    uint64_t             signalValues[]     = { frameNumber };

    VkTimelineSemaphoreSubmitInfo tsInfo {
        VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, nullptr, 1, waitValues, 1, signalValues };
    VkSubmitInfo submitInfo {
        VK_STRUCTURE_TYPE_SUBMIT_INFO, &tsInfo,
        1, waitSemaphores, waitStages, 1, &cmdBuffer, 1, signalSemaphores };

    result = vkQueueSubmit(graphicsQueue, 1, &submitInfo, gpuBusy);
    if (result != VK_SUCCESS) throw std::runtime_error("Could not submit cmdbuffer!");
}

SharedFrameHandles Renderer::ExportSharedHandles() const {
    return SharedFrameHandles {
        sharedColorInfo.kmtHandle, sharedColorInfo.allocationSize, sharedColorInfo.memoryTypeIndex,
        sharedColorFormat, sharedColorExtent, sharedColorUsage,
        renderReadySemHandle, copyDoneSemHandle
    };
}

void Renderer::ReleaseExportedhandles() {
    CloseHandle(sharedColorInfo.kmtHandle);   // really an NT handle now; consider renaming the field
    CloseHandle(renderReadySemHandle);
    CloseHandle(copyDoneSemHandle);
}

uint32_t Renderer::SearchMemoryType(uint32_t typeBits, VkMemoryPropertyFlags mpFlags) {
    VkPhysicalDeviceMemoryProperties memProps{};

    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1 << i)) && ((memProps.memoryTypes[i].propertyFlags & mpFlags) == mpFlags)) {
            return i;
        }
    }

    throw std::runtime_error("Could not find suitable memory type!");
}

Renderer::BufferInfo Renderer::CreateBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memPropFlags, VkDeviceSize size) {
    VkBuffer        buffer       = VK_NULL_HANDLE;
    VkDeviceMemory  deviceMem    = VK_NULL_HANDLE;
    VkResult result;

    VkBufferCreateInfo bufferCreateInfo {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        nullptr,
        0, // reserved
        size,
        usageFlags,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr
    };

    result = vkCreateBuffer(device, &bufferCreateInfo, nullptr, &buffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create buffer!");
    }

    VkMemoryRequirements memRequirement{};
    vkGetBufferMemoryRequirements(device, buffer, &memRequirement);

    uint32_t index = SearchMemoryType(memRequirement.memoryTypeBits, memPropFlags);

    VkMemoryAllocateInfo allocInfo {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        nullptr,
        memRequirement.size,
        index
    };

    result = vkAllocateMemory(device, &allocInfo, nullptr, &deviceMem);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create vertex buffer!");
    }

    vkBindBufferMemory(device, buffer, deviceMem, 0);

    return BufferInfo{ buffer, deviceMem, nullptr };
}

void Renderer::DestroyBuffer(BufferInfo& buffInfo, bool defer) {
    auto deleter = 
        [ cdevice  = device
        , info     = buffInfo ]
        {
            if (info.cpuVA) {
                vkUnmapMemory(cdevice, info.memory);
            }

            vkFreeMemory(cdevice, info.memory, nullptr);
            vkDestroyBuffer(cdevice, info.buffer, nullptr);
        };

    if (defer) {
        deletionQueue.Append(deleter);
    }
    else {
        deleter();
    }
}

void Renderer::CopyBuffer(VkCommandBuffer cmdBuffer, VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    VkBufferCopy bufferCopy {
        0,
        0,
        size
    };

    vkCmdCopyBuffer(cmdBuffer, src, dst, 1, &bufferCopy);
}

VkImageView Renderer::CreateImageView(VkImage image, VkFormat imageFormat, VkImageAspectFlags aspectFlags) {
    VkImageView imageView;
    VkResult result;

    VkImageViewCreateInfo createInfo {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        nullptr,
        0,
        image,
        VK_IMAGE_VIEW_TYPE_2D,
        imageFormat,
        { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY },
        { aspectFlags, 0, 1, 0, 1 }
    };

    result = vkCreateImageView(device, &createInfo, nullptr, &imageView);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create a swap chain image view!");
    }

    return imageView;
}

VkFormat Renderer::findSuitableFormat(const std::vector<VkFormat>& formats, const VkImageTiling tiling, VkFormatFeatureFlags const features) {
    for (VkFormat fmt : formats) {
        VkFormatProperties props;

        vkGetPhysicalDeviceFormatProperties(physicalDevice, fmt, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && ((props.linearTilingFeatures & features) == features)) {
            return fmt;
        }
        else if(tiling == VK_IMAGE_TILING_OPTIMAL && ((props.optimalTilingFeatures & features) == features)) {
            return fmt;
        }
    }

    throw std::runtime_error("Could not find suitable format!");
}

Renderer::ImageInfo Renderer::CreateImage(VkFormat format, VkImageTiling tiling, VkImageUsageFlags usageFlags, VkMemoryPropertyFlags memPropFlags, VkImageAspectFlags aspectFlags, uint32_t width, uint32_t height) {
    VkDeviceMemory memory;
    VkImage        image;
    VkResult       result;

    VkImageCreateInfo createInfo {
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        nullptr,
        0, // flags
        VK_IMAGE_TYPE_2D,
        format,
        VkExtent3D { width, height, 1 },
        1,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        tiling,
        usageFlags,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr,
        VK_IMAGE_LAYOUT_UNDEFINED
    };

    result = vkCreateImage(device, &createInfo, nullptr, &image);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        nullptr,
        memRequirements.size,
        SearchMemoryType(memRequirements.memoryTypeBits, memPropFlags)
    };

    result = vkAllocateMemory(device, &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not allocate image memory!");
    }

    result = vkBindImageMemory(device, image, memory, 0);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not bind image memory!");
    }

    return {image, memory, CreateImageView(image, format, aspectFlags) };
}

void Renderer::DestroyImage(ImageInfo& imgInfo, bool defer) {
    auto deleter = 
        [ cdevice  = device
        , info     = imgInfo ]
        {
            vkDestroyImageView(cdevice, info.view, nullptr);
            vkFreeMemory(cdevice, info.memory, nullptr);
            vkDestroyImage(cdevice, info.image, nullptr);
        };

    if (defer) {
        deletionQueue.Append(deleter);
    }
    else {
        deleter();
    }
}

void Renderer::CopyBufferToImage(VkCommandBuffer cmdBuffer, VkBuffer src, VkImage image, uint32_t width, uint32_t height) {
    VkBufferImageCopy region {
        0, // bufferOffset
        0, // bufferRowLength
        0, // bufferImageHeight
        {  // imageSubresource
            VK_IMAGE_ASPECT_COLOR_BIT,
            0,
            0,
            1,
    },
    VkOffset3D{ 0, 0, 0 },
    VkExtent3D{ width, height, 1}
    };

    vkCmdCopyBufferToImage(cmdBuffer, src, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

VkCommandBuffer Renderer::BeginOneTimeCommands() {
    VkResult result;
    VkCommandBuffer cmdBuffer;

    VkCommandBufferAllocateInfo allocInfo {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        nullptr,
        commandPoolTx,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        1
    };

    result = vkAllocateCommandBuffers(device, &allocInfo, &cmdBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not allocate commandbuffer!");
    }

    VkCommandBufferBeginInfo beginInfo{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        nullptr
    };

    result = vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not begin commandbuffer!");
    }

    return cmdBuffer;
}

void Renderer::EndOneTimeCommands(VkCommandBuffer cmdBuffer) {
    VkResult result;

    result = vkEndCommandBuffer(cmdBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not end commandbuffer!");
    }

    VkSubmitInfo submitInfo { 
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        0,
        nullptr,
        nullptr,
        1,
        &cmdBuffer,
        0,
        nullptr
    };

    result = vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not submit transfer command buffer!");
    }

    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPoolTx, 1, &cmdBuffer);
}

/////////////////////////////////////////////////////////////////////////////////////////////

static void MakeConsole() {
    if (AllocConsole()) {
        AttachConsole(GetCurrentProcessId());
        SetConsoleTitle(TEXT(APPLICATION_NAME));

        FILE* fpDummy;

        // 2. Redirect standard output (stdout) to the console's active screen buffer
        // Using "CONOUT$" ensures it maps to the newly allocated console output
        freopen_s(&fpDummy, "CONOUT$", "w", stdout);

        // 3. Redirect standard error (stderr)
        freopen_s(&fpDummy, "CONOUT$", "w", stderr);

        // 4. Redirect standard input (stdin) if you want to read user input
        freopen_s(&fpDummy, "CONIN$", "r", stdin);

        // 5. Synchronize the C++ iostreams (std::cout, std::cin, etc.) with the CRT
        std::ios::sync_with_stdio();

        // Optional: Clear out any previous bad states the iostream might have cached
        std::cout.clear();
        std::cerr.clear();
        std::cin.clear();
    }
}

int main(int argc, char* argv[]) {
    HINSTANCE hinstance = NULL;
    MakeConsole();

    Presenter presenter;
    if (!presenter.Init(hinstance)) {
        return -1;
    }

    Renderer renderer;
    if (!renderer.Init(presenter.GetChosenDeviceUUID(), presenter.GetSwapChainImageFormat(), presenter.GetSwapChainImageExtent())) {
        return -1;
    }

    presenter.ImportSharedResources(renderer.ExportSharedHandles());
    renderer.ReleaseExportedhandles();

    uint64_t frameNumber = 0;

    for (;;) {
        MSG msg;

        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (msg.message == WM_QUIT) {
            break;
        }

        ++frameNumber;
        renderer.RenderFrame(frameNumber);
        presenter.PresentFrame(frameNumber);
    }

    presenter.WaitIdle();
    renderer.WaitIdle();

    presenter.Shutdown();   // must complete before renderer.Shutdown() � Presenter imports Renderer's resources
    renderer.Shutdown();

    return 0;
}

