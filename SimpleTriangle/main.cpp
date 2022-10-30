#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <vulkan/vulkan.h>
#include <iostream>
#include <vector>

class alignas(64) Harmony {
public:

    bool Init();
    void Shutdown();

private:
    void CreateInstance();
    void DestroyInstance();



    VkInstance instance;

};

bool Harmony::Init() {
    try {
        CreateInstance();

    }
    catch (std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        return false;
    }

    return true;
}

void Harmony::Shutdown() {
    try {

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
}

void Harmony::DestroyInstance() {
    vkDestroyInstance(instance, nullptr);
}


static void MakeConsole() {
    AllocConsole();

    FILE* fDummy;
    freopen_s(&fDummy, "CONIN$", "r", stdin);
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
