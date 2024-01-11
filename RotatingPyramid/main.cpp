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
    void Resize();

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

    struct BufferInfo {
        VkBuffer        buffer = VK_NULL_HANDLE;
        VkDeviceMemory  memory = VK_NULL_HANDLE;
        void*           cpuVA  = nullptr; 
    };

    struct ImageInfo {
        VkImage         image  = VK_NULL_HANDLE;
        VkDeviceMemory  memory = VK_NULL_HANDLE;
        VkImageView     view   = VK_NULL_HANDLE;
    };

    void CreateInstance();
    void OpenWindow(HINSTANCE instance);
    void CreateSurface(HINSTANCE instance);
    void ChoosePhysicalDevice();
    void CreateLogicalDevice();
    void CreateSwapChain();
    void CreateCommandPoolAndBuffers();
    void CreateSyncObjects();
    void CreateImageViews();

    void CreateRenderPass();
    void CreateUniformBuffer();
    void CreateVertexBuffer(VkCommandBuffer cmdBuffer);
    void CreateIndexBuffer(VkCommandBuffer cmdBuffer);
    void CreateTextureImageAndView(VkCommandBuffer cmdBuffer);
    void CreateTextureSampler();
    void CreateDepthImageAndView();

    void CreateDescriptorPoolAndSets();

    void CreateFrameBuffers();
    void CreateDescriptorSetLayout();
    void CreateGraphicsPipeline();
    
    void UpdateUbo(uint32_t imageIndex);
    void RecordCommandBuffer(VkCommandBuffer cmdBuffer, uint32_t imageIndex);
    void Render();

    uint32_t SearchMemoryType(uint32_t typeBits, VkMemoryPropertyFlags mpfFlags);

    BufferInfo CreateBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memPropFlags, VkDeviceSize size);
    void DestroyBuffer(BufferInfo& buffInfo, bool defer = false);
    
    ImageInfo CreateImage(VkFormat format, VkImageTiling tiling, VkImageUsageFlags usageFlags, VkMemoryPropertyFlags memPropFlags, VkImageAspectFlags aspectFlags, uint32_t width, uint32_t height);
    void DestroyImage(ImageInfo& imgInfo, bool defer=false);

    void CopyBuffer(VkCommandBuffer cmdBuffer, VkBuffer src, VkBuffer dst, VkDeviceSize size);
    void CopyBufferToImage(VkCommandBuffer cmdBuffer, VkBuffer src, VkImage image, uint32_t width, uint32_t height);
    void TransitionImage(VkCommandBuffer cmdBuffer, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageLayout oldLayout, VkImageLayout newLayout);

    VkCommandBuffer BeginOneTimeCommands();
    void EndOneTimeCommands(VkCommandBuffer cmdBuffer);

    VkImageView CreateImageView(VkImage image, VkFormat imageFormat, VkImageAspectFlags aspectFlags);

    VkFormat findSuitableFormat(const std::vector<VkFormat>& formats, VkImageTiling tiling, VkFormatFeatureFlags features);

    void OnWindowSizeChanged();

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam,
        LPARAM lParam);

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, 
        VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

    static bool HasStencilComponent(VkFormat format) {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    static std::vector<char> readShaderFile(const std::string& filePath);

    using SwapChainImageVec       = std::vector<VkImage>;
    using SwapChainImageViewVec   = std::vector<VkImageView>;
    using SwapChainFramebufferVec = std::vector<VkFramebuffer>;
    using CmdBufferVec            = std::vector<VkCommandBuffer>;
    using SemaphoreVec            = std::vector<VkSemaphore>;
    using FenceVec                = std::vector<VkFence>;
    using DescriptorSetVec        = std::vector<VkDescriptorSet>;
    using UboVec                  = std::vector<BufferInfo>;
    
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
    VkDescriptorSetLayout    descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout         pipelineLayout      = VK_NULL_HANDLE;
    VkPipeline               graphicsPipeline    = VK_NULL_HANDLE;
    VkCommandPool            commandPool         = VK_NULL_HANDLE;
    VkCommandPool            commandPoolTx       = VK_NULL_HANDLE;
    VkDescriptorPool         descriptorPool      = VK_NULL_HANDLE;
    VkSampler                sampler             = VK_NULL_HANDLE;

    PFN_vkGetPipelineExecutablePropertiesKHR  vkGetPipelineExecutableProperties = VK_NULL_HANDLE;
    PFN_vkGetPipelineExecutableInternalRepresentationsKHR vkGetPipelineExecutableInternalRepresentations = VK_NULL_HANDLE;

    VkBool32                 windowResized       = VK_FALSE;

    uint64_t                 currentFrame        = 0;

    BufferInfo               vertexBufferInfo;
    BufferInfo               indexBufferInfo;
    ImageInfo                textureInfo;
    ImageInfo                depthInfo;

    DeletionQueue            deletionQueue;

    CmdBufferVec             cmdBufferVec;
    SemaphoreVec             imageReadyVec;
    SemaphoreVec             renderCompleteVec;
    FenceVec                 gpuBusyVec;
    DescriptorSetVec         descSetVec;
    SwapChainImageVec        swapChainImageVec;
    SwapChainImageViewVec    swapChainImageViewVec;
    SwapChainFramebufferVec  swapChainFramebufferVec;

    UboVec                   uboVec;
    std::array<PushConstant, MAX_FRAMES_IN_FLIGHT>  pushConstantVec;

    QueueFamilyIndices          choosenQueueIndices;
    VkPhysicalDeviceProperties2 chosenDeviceProps;
    VkPhysicalDeviceFeatures2   choosenDeviceFeatures;
    
    VkFormat                 swapChainImageFormat;
    VkFormat                 depthFormat;
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

    case WM_SIZE:
        {
            Harmony* pApp = reinterpret_cast<Harmony* >(GetWindowLongPtr(hWnd, GWLP_USERDATA));
            if( pApp) {
                pApp->Resize();
                return 0;
            }
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
    std::string fd;

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

        CreateCommandPoolAndBuffers();

        CreateSyncObjects();

        CreateImageViews();

        CreateDepthImageAndView();

        CreateRenderPass();

        ////////////////////////////////////
        // resources
        auto cmdBuffer = BeginOneTimeCommands();

        CreateVertexBuffer(cmdBuffer);

        CreateIndexBuffer(cmdBuffer);

        CreateTextureImageAndView(cmdBuffer);

        EndOneTimeCommands(cmdBuffer);

        ////////////////////////////////////

        CreateTextureSampler();

        CreateUniformBuffer();

        CreateFrameBuffers();

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

void Harmony::Resize() {
    windowResized = VK_TRUE;
}

#pragma endregion
/////////////////////////////////////////////////////////////////////////////////////////////
#pragma region Init Calls

void Harmony::CreateInstance() {
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
        throw std::runtime_error("Could not find all requiured extensions!");
    }

    VkApplicationInfo applicationInfo {
        VK_STRUCTURE_TYPE_APPLICATION_INFO,
        nullptr,
        "SimpleTriangle",
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

    SetWindowLongPtr(hMainWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
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
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME
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
    auto rateDevice = [&](VkPhysicalDevice pd, QueueFamilyIndices& indices, VkPhysicalDeviceProperties2 &props, VkPhysicalDeviceFeatures2 &feats) -> int {
        VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR plExecFeats {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR,
            nullptr,
            0
        };

        VkPhysicalDeviceProperties2 deviceProps {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            nullptr,
        };

        VkPhysicalDeviceFeatures2   deviceFeats {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            &plExecFeats,
        };

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

        vkGetPhysicalDeviceProperties2(pd, &deviceProps);
        if (deviceProps.properties.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1500;
        }
        else if (deviceProps.properties.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            score += 500;
        }

        if (deviceProps.properties.limits.maxPushConstantsSize < sizeof(PushConstant)) {
            return 0;
        }

        // we need VK 1.3 for dynamic rendering
        uint32_t major = VK_API_VERSION_MAJOR(deviceProps.properties.apiVersion);
        uint32_t minor = VK_API_VERSION_MINOR(deviceProps.properties.apiVersion);

        if (major != 1 && minor < 3) {
            return 0;
        }

        vkGetPhysicalDeviceFeatures2(pd, &deviceFeats);
        if (deviceFeats.features.multiDrawIndirect) {
            score += 200;
        }

        props = deviceProps;
        feats = deviceFeats;

        return score;
    };

    result = vkEnumeratePhysicalDevices(instance, &itemCount, nullptr);
    if ((result != VK_SUCCESS) || itemCount == 0) {
        throw std::runtime_error("Could not find amy Vulkan capble GPU!");
    }

    struct DeviceAndQueueInfo {
        VkPhysicalDevice            physicalDevice;
        VkPhysicalDeviceProperties2 deviceProps;
        VkPhysicalDeviceFeatures2   deviceFeats;
        QueueFamilyIndices          indices;
    };

    std::vector<VkPhysicalDevice>     physDeviceVec(itemCount);
    std::map<int, DeviceAndQueueInfo> deviceMap;

    do {
        result = vkEnumeratePhysicalDevices(instance, &itemCount, physDeviceVec.data());
    } while (result == VK_INCOMPLETE);

    for (auto& physicalDevice : physDeviceVec) {
        QueueFamilyIndices indices{};
        VkPhysicalDeviceProperties2 props{};
        VkPhysicalDeviceFeatures2 feats{};

        int score = rateDevice(physicalDevice, indices, props, feats);

        deviceMap[score] = { physicalDevice, props, feats, indices };
    }

    if (deviceMap.empty()) {
        throw std::runtime_error("Could not find suitable device!");
    }

    DeviceAndQueueInfo myDevice = deviceMap.rbegin()->second;

    physicalDevice        = myDevice.physicalDevice;
    choosenQueueIndices   = myDevice.indices;
    chosenDeviceProps     = myDevice.deviceProps;
    choosenDeviceFeatures = myDevice.deviceFeats;
}

void Harmony::CreateLogicalDevice() {
    VkResult result;

    std::vector<const char*> requiredExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME
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

    VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR plFeats {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR,
        nullptr,
        VK_TRUE
    };

    VkDeviceCreateInfo deviceCreateInfo {
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        &plFeats,
        0,                           // no flags
        static_cast<uint32_t>(queueCreateInfoVec.size()),
        queueCreateInfoVec.data(),   // queues
        0,
        nullptr,                     // deprecated
        static_cast<uint32_t>(requiredExtensions.size()),
        requiredExtensions.data(),   // device extensions
        &choosenDeviceFeatures.features,
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

    this->vkGetPipelineExecutableProperties = (PFN_vkGetPipelineExecutablePropertiesKHR)vkGetDeviceProcAddr(device, "vkGetPipelineExecutablePropertiesKHR");
    this->vkGetPipelineExecutableInternalRepresentations = (PFN_vkGetPipelineExecutableInternalRepresentationsKHR)vkGetDeviceProcAddr(device, "vkGetPipelineExecutableInternalRepresentationsKHR");
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

void Harmony::CreateCommandPoolAndBuffers() {
    VkResult result;

    // graphics command pool & command buffers
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

void Harmony::CreateImageViews() {
    swapChainImageViewVec.resize(swapChainImageVec.size());

    for (size_t i = 0; i < swapChainImageViewVec.size(); ++i) {
        swapChainImageViewVec[i] = CreateImageView(swapChainImageVec[i], swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
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
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,         // not using stencil
        VK_ATTACHMENT_STORE_OP_DONT_CARE,        // not using stencil 
        VK_IMAGE_LAYOUT_UNDEFINED,               // don't care what layout was before
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR          // transition to present 
    };

    VkAttachmentDescription depthTarget {
        0,
        depthFormat,
        VK_SAMPLE_COUNT_1_BIT,                   // samples 
        VK_ATTACHMENT_LOAD_OP_CLEAR,             // clear on load  
        VK_ATTACHMENT_STORE_OP_DONT_CARE,        // don't care what happens to depth values after pass
        VK_ATTACHMENT_LOAD_OP_DONT_CARE,         // not using stencil
        VK_ATTACHMENT_STORE_OP_DONT_CARE,        // not using stencil 
        VK_IMAGE_LAYOUT_UNDEFINED,               // don't care what layout was before
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL          // transition to depth stencil
    };

    VkAttachmentReference renderTargetAttachmentRef {
        0,                                       // attachment is referred to at layout = 0 in shader
        VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL       // accessed optimally
    };

    VkAttachmentReference depthAttachmentRef {
        1,                                                  // attachment is referred to at layout = 0 in shader
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL    // accessed optimally
    };

    VkSubpassDescription subPassDesc {
        0,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        0,                    // 0 input attachments
        nullptr,
        1,                    // 1 color attachments
        &renderTargetAttachmentRef, 
        nullptr,              // no msaa
        &depthAttachmentRef,  // one and only depth stencil
        0,                    // nothing to preserve
        nullptr,
    };

    VkSubpassDependency dependency {
        VK_SUBPASS_EXTERNAL,
        0,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        0
    };

    std::array<VkAttachmentDescription, 2> attachments = {renderTarget, depthTarget};

    VkRenderPassCreateInfo rpCreateInfo {
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        nullptr,
        0,
        static_cast<uint32_t>(attachments.size()),
        attachments.data(),
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

void Harmony::CreateUniformBuffer() {
    uboVec.resize(MAX_FRAMES_IN_FLIGHT);

    VkDeviceSize uboSize = sizeof(glm::mat4);

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        auto info = CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uboSize);

        vkMapMemory(device, info.memory, 0, uboSize, 0, &info.cpuVA);

        uboVec[i] = info;

        // delete at app exit
        DestroyBuffer(info, true);
    }
}

void Harmony::CreateVertexBuffer(VkCommandBuffer cmdBuffer) {
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

void Harmony::CreateIndexBuffer(VkCommandBuffer cmdBuffer) {
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

void Harmony::CreateTextureImageAndView(VkCommandBuffer cmdBuffer) {
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

void Harmony::CreateDepthImageAndView() {
    depthFormat = findSuitableFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

    depthInfo = CreateImage(depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, swapChainImageExtent.width, swapChainImageExtent.height);
    DestroyImage(depthInfo, true);

    auto cmdBuffer = BeginOneTimeCommands();
    TransitionImage(cmdBuffer, depthInfo.image, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    EndOneTimeCommands(cmdBuffer);
}

void Harmony::CreateTextureSampler() {
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

void Harmony::CreateDescriptorPoolAndSets() {
    VkResult result;

    std::array<VkDescriptorPoolSize, 2> poolSizes = {
        {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_IN_FLIGHT }
        }
    };

    VkDescriptorPoolCreateInfo createInfo {
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        nullptr,
        0,
        MAX_FRAMES_IN_FLIGHT,
        2,
        poolSizes.data()
    };

    result = vkCreateDescriptorPool(device, &createInfo, nullptr, &descriptorPool);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create descriptor pool!");
    }

    deletionQueue.Append(
        [cdevice = device,
        cpool = descriptorPool]
        {
            vkDestroyDescriptorPool(cdevice, cpool, nullptr);
        }
    );

    descSetVec.resize(MAX_FRAMES_IN_FLIGHT);

    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);

    VkDescriptorSetAllocateInfo allocInfo {
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        nullptr,
        descriptorPool,
        MAX_FRAMES_IN_FLIGHT,
        layouts.data()
    };

    result = vkAllocateDescriptorSets(device, &allocInfo, descSetVec.data());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not allocate descriptor sets!");
    }

    // init descriptor sets
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorBufferInfo buffInfo {
            uboVec[i].buffer,
            0,
            VK_WHOLE_SIZE 
        };

        VkDescriptorImageInfo imageInfo {
            sampler,
            textureInfo.view,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };

        std::array<VkWriteDescriptorSet, 2> writeDescs = {
            {
                {
                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    nullptr,
                    descSetVec[i],
                    0,
                    0,
                    1,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    nullptr,
                    &buffInfo,
                    nullptr
                },
                {
                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    nullptr,
                    descSetVec[i],
                    1,
                    0,
                    1,
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    &imageInfo,
                    nullptr,
                    nullptr
                }
        } };

        vkUpdateDescriptorSets(device, 2, writeDescs.data(), 0, nullptr);
    }
}

void Harmony::CreateFrameBuffers() {
    VkResult result;

    swapChainFramebufferVec.resize(swapChainImageViewVec.size());

    for (size_t i = 0; i < swapChainFramebufferVec.size(); ++i) {
        VkImageView attachments[] = {
            swapChainImageViewVec[i],
            depthInfo.view
        };

        VkFramebufferCreateInfo fbCreateInfo{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            nullptr,
            0,
            renderPass,
            2,
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

void Harmony::CreateDescriptorSetLayout() {
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

void Harmony::CreateGraphicsPipeline() {
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
#ifdef __DUMP_SHADER_INFO__
    plFlags |= VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR;
#endif

    VkGraphicsPipelineCreateInfo pipelineCreateInfo {
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        nullptr,
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
        renderPass,
        0,
        VK_NULL_HANDLE,
        -1
    };

    result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &graphicsPipeline);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create graphics pipeline!");
    }

#ifdef __DUMP_SHADER_INFO__
    // executable properties
    {
        VkPipelineInfoKHR plInfo {
            VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR,
            nullptr,
            graphicsPipeline
        };

        VkPipelineExecutablePropertiesKHR plProps {
            VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR,
            nullptr,
        };

        uint32_t executableCount = 0;

        vkGetPipelineExecutableProperties(device, &plInfo, &executableCount, nullptr);

        std::cout << "Num executables: " << executableCount << std::endl;
        if (executableCount > 0) {
            std::vector<VkPipelineExecutablePropertiesKHR> plPropsVec(executableCount);
            for (auto& p : plPropsVec) {
                p.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR;
            }
            
            vkGetPipelineExecutableProperties(device, &plInfo, &executableCount, plPropsVec.data());
            for (uint32_t i = 0; i < executableCount; ++i) {
                VkPipelineExecutableInfoKHR info {
                    VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR,
                    nullptr,
                    graphicsPipeline,
                    i
                };

                uint32_t irCount = 0;
                this->vkGetPipelineExecutableInternalRepresentations(device, &info, &irCount, nullptr);
                if( irCount > 0 ) {
                    std::vector<VkPipelineExecutableInternalRepresentationKHR> irVec(irCount);
                    for (auto& x : irVec) {
                        x.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR;
                    }

                    this->vkGetPipelineExecutableInternalRepresentations(device, &info, &irCount, irVec.data());
                    for (auto& x : irVec) {
                        std::cout << plPropsVec[i].description << ", " << x.name << ", " << x.description << std::endl;

                        if (x.isText) {
                            std::string_view view(reinterpret_cast<const char *>(x.pData), x.dataSize);
                            std::cout << view << std::endl;
                        }
                    }
                }
            }
        }
    }
#endif // !__DUMP_SHADER_INFO__

    deletionQueue.Append(
        [ cdevice = device
        , cpipeline = graphicsPipeline ] {
            vkDestroyPipeline(cdevice, cpipeline, nullptr);
        }
    );

    vkDestroyShaderModule(device, fShaderModule, nullptr);
    vkDestroyShaderModule(device, vShaderModule, nullptr);
}

#pragma endregion
/////////////////////////////////////////////////////////////////////////////////////////////
#pragma region Rendering

void Harmony::UpdateUbo(uint32_t imageIndex) {
    static auto epoch = std::chrono::high_resolution_clock::now();

    auto current = std::chrono::high_resolution_clock::now();
    float time   = std::chrono::duration<float, std::chrono::seconds::period>( current - epoch ).count();

    auto model = glm::rotate(
        glm::mat4(1.0f),            // identity
        time * glm::radians(90.0f), // angle
        glm::vec3(0.0f, 1.0f, 0.0f) // which axis to rotate?
    );

    float yDisplacement = (glm::sin(time * 5) * 0.25f) - 0.25f;

    model = glm::translate(model, glm::vec3(0.0f, yDisplacement, 0.0f));

    auto view  = glm::lookAt(
        glm::vec3(0.0f, 0.25f, -1.0f), // eye position 
        glm::vec3(0.0f, 0.0f, 0.0f),  // looking at 
        glm::vec3(0.0f, 1.0f, 0.0f)   // up vector
    );

    auto proj  = glm::perspective(
        glm::radians(70.0f),         // fov
        float(swapChainImageExtent.width) / swapChainImageExtent.height, // aspect ratio
        0.1f,                       // near 
        20.0f                       // far
    );

    // https://github.com/LunarG/VulkanSamples/blob/master/Sample-Programs/Hologram/Hologram.cpp
    // vulkan has inverted Y and half Z
    const glm::mat4 clip = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0, -1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.5f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    pushConstantVec[imageIndex] = { clip * proj * view };

    memcpy_s( uboVec[imageIndex].cpuVA, sizeof(model), &model, sizeof(model));
}

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

    VkClearValue clearValue[2];

    clearValue[0].color = {0.0, 0.0f, 0.0f, 1.0f};
    clearValue[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpBeginInfo {
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        nullptr,
        renderPass,
        swapChainFramebufferVec[imageIndex],
        {{0, 0}, {swapChainImageExtent.width, swapChainImageExtent.height}},
        2,
        clearValue
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

    VkBuffer vbs[] = { vertexBufferInfo.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vbs, offsets);

    vkCmdBindIndexBuffer(cmdBuffer, indexBufferInfo.buffer, 0, VkIndexType::VK_INDEX_TYPE_UINT16);

    vkCmdSetViewport(cmdBuffer, 0, 1, &vp);
    vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descSetVec[imageIndex], 0, nullptr);

    vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstant), &pushConstantVec[imageIndex]);

    vkCmdDrawIndexed(cmdBuffer, 12, 1, 0, 0, 0);

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
    
    result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageReady, VK_NULL_HANDLE, &imageIndex);
    if( result == VK_ERROR_OUT_OF_DATE_KHR || windowResized == VK_TRUE) {
        OnWindowSizeChanged();
        return;
    }

    // reset the fence only if we are submitting work to GPU
    vkResetFences(device, 1, &gpuBusy);

    vkResetCommandBuffer(cmdBuffer, 0);
       UpdateUbo(imageIndex);
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
    
    result = vkQueuePresentKHR(presentQueue, &presentInfo);
    if( result == VK_ERROR_OUT_OF_DATE_KHR || windowResized == VK_TRUE) {
        OnWindowSizeChanged();
        return;
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

#pragma endregion
/////////////////////////////////////////////////////////////////////////////////////////////
#pragma region Misc
uint32_t Harmony::SearchMemoryType(uint32_t typeBits, VkMemoryPropertyFlags mpFlags) {
    VkPhysicalDeviceMemoryProperties memProps{};

    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1 << i)) && ((memProps.memoryTypes[i].propertyFlags & mpFlags) == mpFlags)) {
            return i;
        }
    }

    throw std::runtime_error("Could not find suitable memory type!");
}

Harmony::BufferInfo Harmony::CreateBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memPropFlags, VkDeviceSize size) {
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

void Harmony::DestroyBuffer(BufferInfo& buffInfo, bool defer) {
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

void Harmony::CopyBuffer(VkCommandBuffer cmdBuffer, VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    VkBufferCopy bufferCopy {
        0,
        0,
        size
    };

    vkCmdCopyBuffer(cmdBuffer, src, dst, 1, &bufferCopy);
}

void Harmony::CopyBufferToImage(VkCommandBuffer cmdBuffer, VkBuffer src, VkImage image, uint32_t width, uint32_t height) {
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

void Harmony::TransitionImage(VkCommandBuffer cmdBuffer, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkPipelineStageFlags srcStageFlags = 0;
    VkPipelineStageFlags dstStageFlags = 0;

    auto flags = aspectFlags;
    if (HasStencilComponent(format)) {
        flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    VkImageSubresourceRange range {
        flags,
        0, // mip
        1, // count
        0, // array
        1  // count
    };

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
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        srcStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    else if(oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        
        srcStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStageFlags = VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    }

    vkCmdPipelineBarrier(cmdBuffer,
        srcStageFlags,
        dstStageFlags,
        0,
        0,  nullptr,
        0,  nullptr,
        1, &barrier);
}

Harmony::ImageInfo Harmony::CreateImage(VkFormat format, VkImageTiling tiling, VkImageUsageFlags usageFlags, VkMemoryPropertyFlags memPropFlags, VkImageAspectFlags aspectFlags, uint32_t width, uint32_t height) {
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

void Harmony::DestroyImage(ImageInfo& imgInfo, bool defer) {
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

VkCommandBuffer Harmony::BeginOneTimeCommands() {
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

void Harmony::EndOneTimeCommands(VkCommandBuffer cmdBuffer) {
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

VkImageView Harmony::CreateImageView(VkImage image, VkFormat imageFormat, VkImageAspectFlags aspectFlags) {
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

VkFormat Harmony::findSuitableFormat(const std::vector<VkFormat>& formats, const VkImageTiling tiling, VkFormatFeatureFlags const features) {
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

void Harmony::OnWindowSizeChanged() {
    vkDeviceWaitIdle(device);

    {
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroyFramebuffer(device, swapChainFramebufferVec[i], nullptr);
        }

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            vkDestroyImageView(device, swapChainImageViewVec[i], nullptr);
        }

        vkDestroyImageView(device, depthInfo.view, nullptr);

        vkDestroySwapchainKHR(device, swapchain, nullptr);
    }

    CreateSwapChain();
    CreateImageViews();
    CreateDepthImageAndView();
    CreateFrameBuffers();

    windowResized = VK_FALSE;
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
