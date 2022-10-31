#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>
#include <optional>
#include <map>

class alignas(64) Harmony {
public:
    struct QueueFamilyIndices {
        std::optional<uint32_t>  graphicsFamily;
        std::optional<uint32_t>  computeFamily;
        std::optional<uint32_t>  transferFamily;

        bool isComplete() const {
            return graphicsFamily.has_value() && computeFamily.has_value() && transferFamily.has_value();
        }
    };

    bool Init();
    void Shutdown();

private:
    void CreateInstance();
    void CreateDevice();

    void DestroyInstance();
    void DestroyDevice();

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, 
        VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);

    VkPhysicalDevice    physicalDevice  = VK_NULL_HANDLE;
    VkDevice            device          = VK_NULL_HANDLE;
    VkInstance          instance        = VK_NULL_HANDLE;

    VkDebugUtilsMessengerEXT debugMessenger;

#ifdef _DEBUG
    static inline const bool enableValidationLayers = true;
#else
    static inline const bool enableValidationLayers = false;
#endif
};

VKAPI_ATTR VkBool32 VKAPI_CALL Harmony::DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                      VkDebugUtilsMessageTypeFlagsEXT messageType,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                      void* pUserData) {
    if (messageSeverity > VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
    }

    return VK_FALSE;
}

bool Harmony::Init() {
    try {
        CreateInstance();

        CreateDevice();
    }
    catch (std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        return false;
    }

    return true;
}

void Harmony::Shutdown() {
    try {
        DestroyDevice();

        DestroyInstance();
    }
    catch (std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
    }
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

void Harmony::CreateDevice() {
    uint32_t itemCount = 0;
    VkResult result;

    auto rateDevice = [](VkPhysicalDevice& physicalDevice) -> int {
        VkPhysicalDeviceProperties deviceProps;
        VkPhysicalDeviceFeatures   deviceFeats;

        int score = 0;

        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProps);
        if (deviceProps.deviceType == VkPhysicalDeviceType::VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        }

        vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeats);
        
        return score;
    };

    auto findQueueFamily = [](VkPhysicalDevice physicalDevice) -> QueueFamilyIndices {
        QueueFamilyIndices index;
        uint32_t qfCount = 0;

        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, nullptr);
        if (qfCount) {
            std::vector<VkQueueFamilyProperties> queueFamilyProps(qfCount);
            uint32_t i = 0;

            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qfCount, queueFamilyProps.data());
            for (auto& qf : queueFamilyProps) {
                if (qf.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    index.graphicsFamily = i;
                }

                if (qf.queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    index.computeFamily = i;
                }

                if (qf.queueFlags & VK_QUEUE_TRANSFER_BIT) {
                    index.transferFamily = i;
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

    VkDeviceQueueCreateInfo qCreateInfo{
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        nullptr,
        0,
        index.graphicsFamily.value(),
        1,
        &queuePriority
    };

    VkDeviceCreateInfo dCreateInfo{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        nullptr,
        0,            // no flags
        1,
        &qCreateInfo, // single queue
        0,
        nullptr,      // deprecated
        0,
        nullptr,      // no device extensions yet  
        nullptr       // default features 
    };

    result = vkCreateDevice(physicalDevice, &dCreateInfo, nullptr, &device);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Could not create logical device!");
    }
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

    FILE* fDummy = nullptr;
    freopen_s(&fDummy, "CONIN$",  "r",  stdin);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
}

int main(int argc, char* argv[]) {
    MakeConsole();

    Harmony app;

    if (!app.Init()) {
        return -1;
    }

    app.Shutdown();
    return 0;
}
