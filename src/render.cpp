#include "render.h"
#include <algorithm>

const std::vector<const char*> validation_layers = {
    "VK_LAYER_KHRONOS_validation"
};

const bool enable_validation_layers = true;

bool check_validation_layer_support() {
    std::vector<vk::LayerProperties> layers = vk::enumerateInstanceLayerProperties();

    for(const auto& v_layer : validation_layers) {
        bool found = false;
        for(const auto& layer : layers) {
            if(std::string(layer.layerName) == std::string(v_layer)) {
                found = true;
            }
        }

        if(!found) {
            return false;
        }
    }

    return true;
}

Render::Render(int width, int height, std::string name) : width(width), height(height), name(name) {
    init_window();
    init_vulkan();

    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }
}

Render::~Render() {

    device.destroy();
    instance.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
}

void Render::init_window() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    window = glfwCreateWindow(width, height, name.c_str(), nullptr, nullptr);
}

void Render::init_vulkan() {
    if(enable_validation_layers && !check_validation_layer_support()) {
        std::cerr << "No validation layer support\n";
        std::exit(EXIT_FAILURE);
    }

    vk::ApplicationInfo app_info(name.c_str(), 1, name.c_str(), 1, VK_API_VERSION_1_3);
    vk::InstanceCreateInfo instance_info({}, &app_info);
    if(enable_validation_layers) {
        instance_info.enabledLayerCount = validation_layers.size();
        instance_info.ppEnabledLayerNames = validation_layers.data();
    }
    uint32_t glfw_extensions_count = 0;
    auto glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extensions_count);
    instance_info.enabledExtensionCount = glfw_extensions_count;
    instance_info.ppEnabledExtensionNames = glfw_extensions;

    instance = vk::createInstance(instance_info);

    vk::PhysicalDevice phys_device = instance.enumeratePhysicalDevices().front();
    std::vector<vk::QueueFamilyProperties> queue_family_properties = phys_device.getQueueFamilyProperties();

    auto property_iterator = std::find_if(queue_family_properties.begin(), queue_family_properties.end(),
    [](vk::QueueFamilyProperties const& qfp) {return qfp.queueFlags & vk::QueueFlagBits::eGraphics;});

    graphics_qf_index = std::distance(queue_family_properties.begin(), property_iterator);
    assert(graphics_qf_index < queue_family_properties.size());

    float queue_priority = 0.0f;
    vk::DeviceQueueCreateInfo queue_info(vk::DeviceQueueCreateFlags(), static_cast<uint32_t>(graphics_qf_index), 1, &queue_priority);
    device = phys_device.createDevice(vk::DeviceCreateInfo(vk::DeviceCreateFlags(), queue_info));

    graphics_queue = device.getQueue(graphics_qf_index, 0);

    {
        VkSurfaceKHR _surface;
        glfwCreateWindowSurface(instance, window, nullptr, &_surface);
        surface = vk::SurfaceKHR(_surface);
    }

    
}
