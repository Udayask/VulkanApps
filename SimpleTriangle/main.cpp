#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <optional>
#include <map>

#define APPLICATION_NAME        "SimpleTriangle"
#define WINDOW_WIDTH            1920
#define WINDOW_HEIGHT           1080

class alignas(64) Harmony {
public:
    struct QueueFamilyIndices {
        std::optional<uint32_t>  graphicsFamily;
        std::optional<uint32_t>  computeFamily;
        std::optional<uint32_t>  transferFamily;
        std::optional<uint32_t>  presentFamily;

        bool isComplete() const {
            return graphicsFamily.has_value() && computeFamily.has_value() && transferFamily.has_value() && presentFamily.has_value();
        }
    };

    struct SwapChain {
    private:
        VkInstance          instance;
        VkPhysicalDevice    physDevice;
        VkDevice            device;

        PFN_vkGetPhysicalDeviceSurfaceSupportKHR        pfnVkGetPhysicalDeviceSurfaceSupportKHR;
        PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR   pfnVkGetPhysicalDeviceSurfaceCapabilitiesKHR;
        PFN_vkGetPhysicalDeviceSurfaceFormatsKHR        pfnVkGetPhysicalDeviceSurfaceFormatsKHR;
        PFN_vkGetPhysicalDeviceSurfacePresentModesKHR   pfnVkGetPhysicalDeviceSurfacePresentModesKHR;

        PFN_vkCreateSwapchainKHR     pfnVkCreateSwapchainKHR;
        PFN_vkDestroySwapchainKHR    pfnVkDestroySwapchainKHR;
        PFN_vkGetSwapchainImagesKHR  pfnVkGetSwapchainImagesKHR;
        PFN_vkAcquireNextImageKHR    pfnVkAcquireNextImageKHR;
        PFN_vkQueuePresentKHR        pfnVkQueuePresentKHR;

    public:
        void init(VkInstance inst, VkPhysicalDevice phyDev, VkDevice dev) {
            instance   = inst;
            physDevice = phyDev;
            device     = dev;

            pfnVkGetPhysicalDeviceSurfaceSupportKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
                vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceSupportKHR"));
            pfnVkGetPhysicalDeviceSurfaceCapabilitiesKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
                vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
            pfnVkGetPhysicalDeviceSurfaceFormatsKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
                vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR"));
            pfnVkGetPhysicalDeviceSurfacePresentModesKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(
                vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfacePresentModesKHR"));

            pfnVkCreateSwapchainKHR = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
                vkGetDeviceProcAddr(device, "vkCreateSwapchainKHR"));
            pfnVkDestroySwapchainKHR = reinterpret_cast<PFN_vkDestroySwapchainKHR>(
                vkGetDeviceProcAddr(device, "vkDestroySwapchainKHR"));
            pfnVkGetSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
                vkGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR"));
            pfnVkAcquireNextImageKHR = reinterpret_cast<PFN_vkAcquireNextImageKHR>(
                vkGetDeviceProcAddr(device, "vkAcquireNextImageKHR"));
            pfnVkQueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(
                vkGetDeviceProcAddr(device, "vkQueuePresentKHR"));
        }
    } swapChain;

    bool Init(HINSTANCE instance);
    void Run();
    void Shutdown(HINSTANCE instance);

private:
    void OpenWindow(HINSTANCE instance);
    void CreateInstance();
    void AttachWindow(HINSTANCE instance);
    void CreateDevice();
    void InitSwapchain();
    
    void Render();

    void DetachWindow();
    void CloseWindow(HINSTANCE instance);
    void DestroyInstance();
    void DestroyDevice();

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam,
        LPARAM lParam);

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, 
        VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

    HWND                hMainWindow         = NULL;

    VkPhysicalDevice    physicalDevice      = VK_NULL_HANDLE;
    VkDevice            device              = VK_NULL_HANDLE;
    VkSurfaceKHR        surface             = VK_NULL_HANDLE;
    VkInstance          instance            = VK_NULL_HANDLE;

    VkQueue             graphicsQueue       = VK_NULL_HANDLE;
    VkQueue             presentQueue        = VK_NULL_HANDLE;
    
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;

#ifdef _DEBUG
    static inline const bool enableValidationLayers = true;
#else
    static inline const bool enableValidationLayers = false;
#endif
};

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

bool Harmony::Init(HINSTANCE hinstance) {
    try {
        CreateInstance();

        OpenWindow(hinstance);

        AttachWindow(hinstance);

        CreateDevice();

        InitSwapchain();
    }
    catch (std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        return false;
    }

    return true;
}

void Harmony::Run() {
    while (true) {
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
}

void Harmony::Shutdown(HINSTANCE hinstance) {
    try {
        DestroyDevice();

        DetachWindow();

        CloseWindow(hinstance);

        DestroyInstance();
    }
    catch (std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
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
}

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

void Harmony::AttachWindow(HINSTANCE hinstance) {
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
}

void Harmony::CreateDevice() {
    uint32_t itemCount;
    VkResult result;

    std::vector<const char*> requiredExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    auto rateDevice = [
        re = requiredExtensions
    ](VkPhysicalDevice& physicalDevice) -> int {
        VkPhysicalDeviceProperties  deviceProps;
        VkPhysicalDeviceFeatures    deviceFeats;
        uint32_t                    itemCount = 0;
        VkResult                    result;

        int score = 0;

        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProps);
        if (deviceProps.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        }

        vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeats);
        if (deviceFeats.multiDrawIndirect) {
            score += 200;
        }

        std::vector<const char*> enabledExtensions;

        result = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &itemCount, nullptr);
        if (result == VK_SUCCESS && itemCount) {
            std::vector<VkExtensionProperties>  extPropsVec(itemCount);
            
            do {
                result = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &itemCount, extPropsVec.data());
            } while (result == VK_INCOMPLETE);
            
            for (auto& r : re) {
                for (auto& ext : extPropsVec) {
                    if (std::string(ext.extensionName) == r) {
                        enabledExtensions.emplace_back(r);
                    }
                }
            }
            
        }

        if (re.size() != enabledExtensions.size()) {
            score = 0;
        }

        return score;
    };

    auto findQueueFamily = [
        psurf = surface
    ](VkPhysicalDevice physicalDevice) -> QueueFamilyIndices {
        QueueFamilyIndices index;
        uint32_t qfCount = 0;

        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, nullptr);
        if (qfCount) {
            std::vector<VkQueueFamilyProperties> queueFamilyProps(qfCount);
            uint32_t i = 0;

            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, queueFamilyProps.data());
            for (auto& qf : queueFamilyProps) {
                VkBool32 presentSupported = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, psurf, &presentSupported);

                if (qf.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    index.graphicsFamily = i;
                }

                if (qf.queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    index.computeFamily = i;
                }

                if (qf.queueFlags & VK_QUEUE_TRANSFER_BIT) {
                    index.transferFamily = i;
                }

                if (presentSupported) {
                    index.presentFamily = i;
                }

                if (index.isComplete()) {
                    break;
                }

                ++i;
            }
        }

        return index;
    };

    std::map<int, VkPhysicalDevice> deviceMap;

    result = vkEnumeratePhysicalDevices(instance, &itemCount, nullptr);
    if (result == VK_SUCCESS && itemCount) {
        std::vector<VkPhysicalDevice> physDeviceVec(itemCount);

        do {
            result = vkEnumeratePhysicalDevices(instance, &itemCount, physDeviceVec.data());
        } while (result == VK_INCOMPLETE);

        for (auto& physicalDevice : physDeviceVec) {
            int score = rateDevice(physicalDevice);

            deviceMap[score] = physicalDevice;
        }
    }
    else {
        throw std::runtime_error("Could not find Vulkan device!");
    }

    if (deviceMap.empty()) {
        throw std::runtime_error("Could not find suitable device!");
    }

    physicalDevice = deviceMap.rbegin()->second;

    QueueFamilyIndices index = findQueueFamily(physicalDevice);

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfoVec {
        {
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            nullptr,
            0,
            index.graphicsFamily.value(),
            1,
            &queuePriority
        },
        {
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            nullptr,
            0,
            index.presentFamily.value(),
            1,
            &queuePriority
        }
    };
    
    VkDeviceCreateInfo deviceCreateInfo {
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        nullptr,
        0,                           // no flags
        static_cast<uint32_t>(queueCreateInfoVec.size()),
        queueCreateInfoVec.data(),   // queue
        0,
        nullptr,                     // deprecated
        static_cast<uint32_t>(requiredExtensions.size()),
        requiredExtensions.data(),   // no device extensions yet
        nullptr                      // default features 
    };

    result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create logical device!");
    }

    vkGetDeviceQueue(device, index.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, index.presentFamily.value(), 0, &presentQueue);
}

void Harmony::InitSwapchain() {
    swapChain.init(instance, physicalDevice, device);
}



void Harmony::Render() {

}

void Harmony::DetachWindow() {
    vkDestroySurfaceKHR(instance, surface, nullptr);
}

void Harmony::CloseWindow(HINSTANCE instance) {
    DestroyWindow(hMainWindow);

    UnregisterClass(APPLICATION_NAME, instance);
}

void Harmony::DestroyDevice() {
    vkDestroyDevice(device, nullptr);
}

void Harmony::DestroyInstance() {
    if (enableValidationLayers) {
        auto vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (vkDestroyDebugUtilsMessengerEXT != nullptr) {
            vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        }
    }

    vkDestroyInstance(instance, nullptr);
}

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
