#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <deque>
#include <optional>
#include <functional>
#include <algorithm>
#include <fstream>
#include <map>

#define APPLICATION_NAME        "SimpleTriangle"
#define WINDOW_WIDTH            1920
#define WINDOW_HEIGHT           1080

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
#pragma region ClassDecl
class alignas(64) Harmony {
public:
    static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

    bool Init(HINSTANCE instance);
    void Run();
    void Shutdown(HINSTANCE instance);

private:
    struct QueueFamilyIndices {
        std::optional<uint32_t>  graphicsFamily;
        std::optional<uint32_t>  computeFamily;
        std::optional<uint32_t>  transferFamily;
        std::optional<uint32_t>  presentFamily;

        bool isComplete() const {
            return graphicsFamily.has_value() && computeFamily.has_value() && transferFamily.has_value() && presentFamily.has_value();
        }
    };

    struct SurfaceCaps {
        using SurfaceFormatKHRVec = std::vector<VkSurfaceFormatKHR>;
        using PresentModeKHRVec   = std::vector<VkPresentModeKHR>;

        VkSurfaceCapabilitiesKHR        surfaceCaps;
        SurfaceFormatKHRVec             surfaceFormatVec;
        PresentModeKHRVec               presentModeVec;
    };

    void CreateInstance();
    void OpenWindow(HINSTANCE instance);
    void CreateSurface(HINSTANCE instance);
    void ChoosePhysicalDevice();
    void CreateLogicalDevice();
    void CreateSwapChain();
    void CreateImageViews();
    void CreateRenderPass();
    void CreateGraphicsPipeline();
    void CreateFrameBuffers();
    void CreateCommandPoolAndBuffers();
    void CreateSyncObjects();

    void RecordCommandBuffer(VkCommandBuffer cmdBuffer, uint32_t imageIndex);
    void Render();

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam,
        LPARAM lParam);

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, 
        VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

    static std::vector<char> readShaderFile(const std::string& filePath);

    using SwapChainImageVec       = std::vector<VkImage>;
    using SwapChainImageViewVec   = std::vector<VkImageView>;
    using SwapChainFramebufferVec = std::vector<VkFramebuffer>;
    using CmdBufferVec            = std::vector<VkCommandBuffer>;
    using SemaphoreVec            = std::vector<VkSemaphore>;
    using FenceVec                = std::vector<VkFence>;

    HWND                     hMainWindow         = NULL;

    VkDebugUtilsMessengerEXT debugMessenger      = VK_NULL_HANDLE;

    VkInstance               instance            = VK_NULL_HANDLE;
    VkPhysicalDevice         physicalDevice      = VK_NULL_HANDLE;
    VkDevice                 device              = VK_NULL_HANDLE;
    VkSurfaceKHR             surface             = VK_NULL_HANDLE;
    VkSwapchainKHR           swapchain           = VK_NULL_HANDLE;
    
    VkQueue                  graphicsQueue       = VK_NULL_HANDLE;
    VkQueue                  presentQueue        = VK_NULL_HANDLE;

    VkRenderPass             renderPass          = VK_NULL_HANDLE;
    VkPipelineLayout         pipelineLayout      = VK_NULL_HANDLE;
    VkPipeline               graphicsPipeline    = VK_NULL_HANDLE;
    VkCommandPool            commandPool         = VK_NULL_HANDLE;

    uint32_t                 currentFrame        = 0;

    DeletionQueue            deletionQueue;

    CmdBufferVec             cmdBufferVec;
    SemaphoreVec             imageReadyVec;
    SemaphoreVec             renderCompleteVec;
    FenceVec                 gpuBusyVec;
    SwapChainImageVec        swapChainImageVec;
    SwapChainImageViewVec    swapChainImageViewVec;
    SwapChainFramebufferVec  swapChainFramebufferVec;

    QueueFamilyIndices       choosenQueueIndices;

    VkFormat                 swapChainImageFormat;
    VkExtent2D               swapChainImageExtent;

#ifdef _DEBUG
    static inline const bool enableValidationLayers = true;
#else
    static inline const bool enableValidationLayers = false;
#endif
};

#pragma endregion
/////////////////////////////////////////////////////////////////////////////////////////////
#pragma region Static Members

LRESULT CALLBACK Harmony::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
    {
        PostQuitMessage(0);         // close the application entirely
        return 0;
    };
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

VKAPI_ATTR VkBool32 VKAPI_CALL Harmony::DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                      VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                      void* pUserData) {
    if (messageSeverity > VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    }

    return VK_FALSE;
}

std::vector<char> Harmony::readShaderFile(const std::string& filePath) {
    std::fstream file;
    std::vector<char> fileData;

    file.open(filePath, std::ios::in | std::ios::binary | std::ios::ate );
    if (!file) {
        throw std::runtime_error("Could not open shader module to read!");
    }

    std::size_t fileSize = static_cast<size_t>( file.tellg() );
    file.seekg(0);

    fileData.resize(fileSize);

    file.read(fileData.data(), fileSize);
    file.close();

    return fileData;
}

#pragma endregion
/////////////////////////////////////////////////////////////////////////////////////////////
#pragma region Public interface
bool Harmony::Init(HINSTANCE hinstance) {
    try {
        CreateInstance();

        OpenWindow(hinstance);

        CreateSurface(hinstance);

        ChoosePhysicalDevice();

        CreateLogicalDevice();

        CreateSwapChain();

        CreateImageViews();

        CreateRenderPass();

        CreateGraphicsPipeline();

        CreateFrameBuffers();

        CreateCommandPoolAndBuffers();

        CreateSyncObjects();
    }
    catch (std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        return false;
    }

    return true;
}

void Harmony::Run() {
    for (;;) {
        MSG msg;

        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (msg.message == WM_QUIT) {
            break;
        }

        Render();
    }

    vkDeviceWaitIdle(device);
}

void Harmony::Shutdown(HINSTANCE hinstance) {
    try {
        deletionQueue.Finalize();
    }
    catch (std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
    }
}

#pragma endregion
/////////////////////////////////////////////////////////////////////////////////////////////
#pragma region Init Calls

void Harmony::CreateInstance() {
    uint32_t itemCount = 0;
    VkResult result;

    std::vector<const char*> requiredLayers = {
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

        for (auto& r : requiredLayers) {
            for (auto& lay : layPropsVec) {
                if (std::string(lay.layerName) == r) {
                    enabledLayers.emplace_back(r);
                }
            }
        }
    }

    if (enabledLayers.size() != requiredLayers.size()) {
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
        throw std::runtime_error("Could not find all requiured extensions!");
    }

    VkApplicationInfo applicationInfo {
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        nullptr,
        "SimpleTriangle",
        1,
        "Harmony",
        1,
        VK_MAKE_API_VERSION(0, 1, 0, VK_HEADER_VERSION)
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

void Harmony::OpenWindow(HINSTANCE hinstance) {
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
}

void Harmony::CreateSurface(HINSTANCE hinstance) {
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
 
void Harmony::ChoosePhysicalDevice() {
    uint32_t itemCount = 0;
    VkResult result;

    std::vector<const char*> requiredExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    // queue family lambda
    // expects surface member to be initialized
    auto findQueueFamily = [&](VkPhysicalDevice pd) -> QueueFamilyIndices {
        QueueFamilyIndices indices;
        uint32_t qfCount = 0;

        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCount, nullptr);
        if (!qfCount) {
            return indices;
        }

        std::vector<VkQueueFamilyProperties> queueFamilyProps(qfCount);
        uint32_t i = 0;

        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCount, queueFamilyProps.data());
        for (auto& qf : queueFamilyProps) {
            VkBool32 presentSupported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &presentSupported);

            if (qf.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphicsFamily = i;
            }

            if (qf.queueFlags & VK_QUEUE_COMPUTE_BIT) {
                indices.computeFamily = i;
            }

            if (qf.queueFlags & VK_QUEUE_TRANSFER_BIT) {
                indices.transferFamily = i;
            }

            if (presentSupported) {
                indices.presentFamily = i;
            }

            if (indices.isComplete()) {
                break;
            }

            ++i;
        }

        return indices;
    };

    // device rate lambda
    auto rateDevice = [&](VkPhysicalDevice pd, QueueFamilyIndices& indices) -> int {
        VkPhysicalDeviceProperties  deviceProps;
        VkPhysicalDeviceFeatures    deviceFeats;
        uint32_t                    itemCount = 0;
        VkResult                    result;

        indices = findQueueFamily(pd);
        if (!indices.isComplete()) {
            return 0;
        }

        std::vector<const char*> enabledExtensions;

        result = vkEnumerateDeviceExtensionProperties(pd, nullptr, &itemCount, nullptr);
        if (result == VK_SUCCESS && itemCount) {
            std::vector<VkExtensionProperties>  extPropsVec(itemCount);

            do {
                result = vkEnumerateDeviceExtensionProperties(pd, nullptr, &itemCount, extPropsVec.data());
            } while (result == VK_INCOMPLETE);

            for (auto& r : requiredExtensions) {
                for (auto& ext : extPropsVec) {
                    if (std::string(ext.extensionName) == r) {
                        enabledExtensions.emplace_back(r);
                    }
                }
            }
        }

        if (requiredExtensions.size() != enabledExtensions.size()) {
            return 0;
        }

        int score = 0;

        vkGetPhysicalDeviceProperties(pd, &deviceProps);
        if (deviceProps.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        }

        vkGetPhysicalDeviceFeatures(pd, &deviceFeats);
        if (deviceFeats.multiDrawIndirect) {
            score += 200;
        }

        return score;
    };

    result = vkEnumeratePhysicalDevices(instance, &itemCount, nullptr);
    if ((result != VK_SUCCESS) || itemCount == 0) {
        throw std::runtime_error("Could not find amy Vulkan capble GPU!");
    }

    struct DeviceAndQueueInfo {
        VkPhysicalDevice   physicalDevice;
        QueueFamilyIndices indices;
    };

    std::vector<VkPhysicalDevice>     physDeviceVec(itemCount);
    std::map<int, DeviceAndQueueInfo> deviceMap;

    do {
        result = vkEnumeratePhysicalDevices(instance, &itemCount, physDeviceVec.data());
    } while (result == VK_INCOMPLETE);

    for (auto& physicalDevice : physDeviceVec) {
        QueueFamilyIndices indices = {};

        int score = rateDevice(physicalDevice, indices);

        deviceMap[score] = { physicalDevice, indices };
    }

    if (deviceMap.empty()) {
        throw std::runtime_error("Could not find suitable device!");
    }

    DeviceAndQueueInfo myDevice = deviceMap.rbegin()->second;

    physicalDevice      = myDevice.physicalDevice;
    choosenQueueIndices = myDevice.indices;
}

void Harmony::CreateLogicalDevice() {
    VkResult result;

    std::vector<const char*> requiredExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfoVec;
    
    queueCreateInfoVec.push_back( VkDeviceQueueCreateInfo {
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        nullptr,
        0,
        choosenQueueIndices.graphicsFamily.value(),
        1,
        &queuePriority }
    );

    if (choosenQueueIndices.presentFamily.value() != choosenQueueIndices.graphicsFamily.value()) {
        queueCreateInfoVec.push_back( VkDeviceQueueCreateInfo {
                VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                nullptr,
                0,
                choosenQueueIndices.presentFamily.value(),
                1,
                &queuePriority }
        );
    }

    VkDeviceCreateInfo deviceCreateInfo {
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        nullptr,
        0,                           // no flags
        static_cast<uint32_t>(queueCreateInfoVec.size()),
        queueCreateInfoVec.data(),   // queues
        0,
        nullptr,                     // deprecated
        static_cast<uint32_t>(requiredExtensions.size()),
        requiredExtensions.data(),   // device extensions
        nullptr                      // default features 
    };

    result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create logical device!");
    }

    deletionQueue.Append(
        [ cdevice = device ] {
            vkDestroyDevice(cdevice, nullptr);
        }
    );

    vkGetDeviceQueue(device, choosenQueueIndices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, choosenQueueIndices.presentFamily.value(), 0, &presentQueue);
}

void Harmony::CreateSwapChain() {
    SurfaceCaps sCaps;
    VkResult    result;
    uint32_t    itemCount = 0;

    // surface caps
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &sCaps.surfaceCaps);

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
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
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

void Harmony::CreateImageViews() {
    VkResult result;

    swapChainImageViewVec.resize(swapChainImageVec.size());

    for (size_t i = 0; i < swapChainImageViewVec.size(); ++i) {
        VkImageViewCreateInfo createInfo {
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            nullptr,
            0,
            swapChainImageVec[i],
            VkImageViewType::VK_IMAGE_VIEW_TYPE_2D,
            swapChainImageFormat,
            { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY },
            { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}
        };

        result = vkCreateImageView(device, &createInfo, nullptr, &swapChainImageViewVec[i]);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Could not create a swap chain image view!");
        }
    }

    deletionQueue.Append(
        [&] {
            for (size_t i = 0; i < swapChainImageViewVec.size(); ++i) {
                vkDestroyImageView(device, swapChainImageViewVec[i], nullptr);
            }

            swapChainImageViewVec.clear();
        });
}

void Harmony::CreateRenderPass() {
    VkResult result;

    VkAttachmentDescription renderTarget {
        0,
        swapChainImageFormat,                    // format
        VK_SAMPLE_COUNT_1_BIT,                   // samples 
        VK_ATTACHMENT_LOAD_OP_CLEAR,             // clear on load  
        VK_ATTACHMENT_STORE_OP_STORE,            // store to mem on exit of pass
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,         // not using depth stencil
        VK_ATTACHMENT_STORE_OP_DONT_CARE,        // not using depth stencil 
        VK_IMAGE_LAYOUT_UNDEFINED,               // don't care what layout was before
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR          // transition to present 
    };

    VkAttachmentReference rtAttachmentRef {
        0,                                       // attachment is referred to at layout = 0 in shader
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL       // accessed optimally
    };

    VkSubpassDescription subPassDesc {
        0,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        0,                    // 0 input attachments
        nullptr,
        1,                    // 1 color attachments
        &rtAttachmentRef, 
        nullptr,              // no msaa
        nullptr,              // no depth stencil
        0,                    // nothing to preserve
        nullptr,
    };

    VkSubpassDependency dependency {
        VK_SUBPASS_EXTERNAL,
        0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        0
    };

    VkRenderPassCreateInfo rpCreateInfo {
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        nullptr,
        0,
        1,
        &renderTarget,
        1,
        &subPassDesc,
        1,
        &dependency
    };

    result = vkCreateRenderPass(device, &rpCreateInfo, nullptr, &renderPass);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create renderpass object!");
    }

    deletionQueue.Append(
        [ cdevice = device
        , crenderPass = renderPass ] {
            vkDestroyRenderPass(cdevice, crenderPass, nullptr);
        }
    );
}

void Harmony::CreateGraphicsPipeline() {
    CHAR currentDirectory[MAX_PATH + 1];
    VkResult result;

    GetCurrentDirectory(MAX_PATH, currentDirectory);

    std::string vPath(currentDirectory);
    vPath += "\\shaders\\vert.spv";
    auto vShader = readShaderFile(vPath);

    std::string fPath(currentDirectory);
    fPath += "\\shaders\\frag.spv";
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

    VkPipelineVertexInputStateCreateInfo vfStateCreateInfo {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        nullptr,
        0,
        0,  // vertexBindingDescriptionCount
        nullptr,
        0,  // vertexAttributeDescriptionCount,
        nullptr
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
        VK_FRONT_FACE_CLOCKWISE,
        VK_FALSE, // depthBiasEnable
        0.0f,     // depthBiasConstantFactor
        0.0f,     // depthBiasClamp
        0.0f,     // depthBiasSlopeFactor
        0.0f      // lineWidth
    };

    VkPipelineMultisampleStateCreateInfo msaaStateCreateInfo {
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        nullptr,
        0,
        VK_SAMPLE_COUNT_1_BIT,
        VK_FALSE, // sampleShadingEnable
        1.0f,     // minSampleShading
        nullptr,  // pSampleMask
        VK_FALSE, // alphaToCoverageEnable 
        VK_FALSE  // alphaToOneEnable
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

    VkPipelineLayoutCreateInfo plCreateInfo {
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        nullptr,
        0,
        0,       // setLayoutCOunt
        nullptr, // pSetLayouts
        0,       // pushConstantRangeCount
        nullptr, // pPushConstantRanges
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

    VkGraphicsPipelineCreateInfo pipelineCreateInfo {
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        nullptr,
        0,
        2,
        shaderStagesCreateInfos,
        &vfStateCreateInfo,
        &iaStateCreateInfo,
        nullptr,
        &vpStateCreateInfo,
        &rsStateCreateInfo,
        nullptr,
        nullptr,
        &cbStateCreateInfo,
        &dynStateCreateInfo,
        pipelineLayout,
        renderPass,
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

void Harmony::CreateFrameBuffers() {
    VkResult result;

    swapChainFramebufferVec.resize(swapChainImageViewVec.size());

    for (size_t i = 0; i < swapChainFramebufferVec.size(); ++i) {
        VkImageView attachments[] = {
            swapChainImageViewVec[i]
        };

        VkFramebufferCreateInfo fbCreateInfo{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            nullptr,
            0,
            renderPass,
            1,
            attachments,
            swapChainImageExtent.width,
            swapChainImageExtent.height,
            1
        };

        result = vkCreateFramebuffer(device, &fbCreateInfo, nullptr, &swapChainFramebufferVec[i]);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Could not create framebuffer object!");
        }
    }

    deletionQueue.Append(
        [&] {
            for (size_t i = 0; i < swapChainFramebufferVec.size(); ++i) {
                vkDestroyFramebuffer(device, swapChainFramebufferVec[i], nullptr);
            }

            swapChainFramebufferVec.clear();
        }
    );
}

void Harmony::CreateCommandPoolAndBuffers() {
    VkResult result;

    cmdBufferVec.resize(MAX_FRAMES_IN_FLIGHT);

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
        MAX_FRAMES_IN_FLIGHT
    };

    result = vkAllocateCommandBuffers(device, &cbAllocInfo, cmdBufferVec.data());
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
}

void Harmony::CreateSyncObjects() {
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

#pragma endregion
/////////////////////////////////////////////////////////////////////////////////////////////
#pragma region Rendering

void Harmony::RecordCommandBuffer(VkCommandBuffer cmdBuffer, uint32_t imageIndex) {
    VkResult result;

    VkCommandBufferBeginInfo beginInfo {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr,
        0,
        nullptr
    };

    result = vkBeginCommandBuffer(cmdBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not begin command buffer!");
    }

    VkClearValue clearColor;

    clearColor.color = { 0.0, 0.0f, 0.0f, 1.0f};

    VkRenderPassBeginInfo rpBeginInfo{
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        nullptr,
        renderPass,
        swapChainFramebufferVec[imageIndex],
        {{0, 0}, {swapChainImageExtent.width, swapChainImageExtent.height}},
        1,
        &clearColor
    };

    VkViewport vp {
        0.0f,
        0.0f,
        static_cast<float>(swapChainImageExtent.width),
        static_cast<float>(swapChainImageExtent.height),
        0.0f,
        1.0f
    };

    VkRect2D scissor{
        0,
        0,
        swapChainImageExtent.width,
        swapChainImageExtent.height,
    };

    vkCmdBeginRenderPass(cmdBuffer, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    vkCmdSetViewport(cmdBuffer, 0, 1, &vp);
    vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

    vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmdBuffer);

    result = vkEndCommandBuffer(cmdBuffer);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not end command buffer!");
    }
}

void Harmony::Render() {
    VkResult result;
    uint32_t imageIndex;

    auto& gpuBusy        = gpuBusyVec[currentFrame];
    auto& imageReady     = imageReadyVec[currentFrame];
    auto& renderComplete = renderCompleteVec[currentFrame];
    auto& cmdBuffer      = cmdBufferVec[currentFrame];

    vkWaitForFences(device, 1, &gpuBusy, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &gpuBusy);

    vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageReady, VK_NULL_HANDLE, &imageIndex);

    vkResetCommandBuffer(cmdBuffer, 0);
       RecordCommandBuffer(cmdBuffer, imageIndex);

    VkSemaphore          waitSemaphores[]   = { imageReady };
    VkSemaphore          signalSemaphores[] = { renderComplete };
    VkPipelineStageFlags waitStages[]       = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSwapchainKHR       swapChains[]       = { swapchain };

    VkSubmitInfo submitInfo {
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        1,
        waitSemaphores,
        waitStages,
        1,
        &cmdBuffer,
        1,
        signalSemaphores
    };

    result = vkQueueSubmit(graphicsQueue, 1, &submitInfo, gpuBusy);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not submit cmdbuffer!");
    }

    VkPresentInfoKHR presentInfo {
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        nullptr,
        1,
        signalSemaphores,
        1,
        swapChains,
        &imageIndex,
        nullptr
    };
    
    vkQueuePresentKHR(presentQueue, &presentInfo);

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

#pragma endregion
/////////////////////////////////////////////////////////////////////////////////////////////

static void MakeConsole() {
    AllocConsole();
    AttachConsole(GetCurrentProcessId());
    SetConsoleTitle(TEXT(APPLICATION_NAME));

    FILE* fDummy = nullptr;
    freopen_s(&fDummy, "CONIN$",  "r",  stdin);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
}

int main(int argc, char* argv[]) {
    HINSTANCE instance = NULL;
    MakeConsole();

    Harmony app;

    if (!app.Init(instance)) {
        return -1;
    }

    app.Run();
    app.Shutdown(instance);

    return 0;
}
