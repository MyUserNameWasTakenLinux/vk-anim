#include "render.h"
#include <algorithm>
#include <limits>

const std::vector<const char*> validation_layers = {
    "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> device_extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
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
    init_swapchain();

    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }
}

Render::~Render() {

    for(auto& iv : swapchain_image_views) {
        device.destroyImageView(iv);
    }
    device.destroySwapchainKHR(swapchain);
    device.destroy();
    instance.destroySurfaceKHR(surface);
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
    auto available_extensions = phys_device.enumerateDeviceExtensionProperties();
    bool swapchain_support = false;
    for(auto extension : available_extensions) {
        if(std::string(extension.extensionName) == std::string(VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            swapchain_support = true;
        }
    }
    if(swapchain_support == false) {
        std::cerr << "Swapchain extension not supported by device\n";
        std::exit(EXIT_FAILURE);
    }
    device = phys_device.createDevice(vk::DeviceCreateInfo(vk::DeviceCreateFlags(), queue_info, {}, device_extensions));

    graphics_queue = device.getQueue(graphics_qf_index, 0);

    {
        VkSurfaceKHR _surface;
        glfwCreateWindowSurface(instance, window, nullptr, &_surface);
        surface = vk::SurfaceKHR(_surface);
    }

    if(!phys_device.getSurfaceSupportKHR(static_cast<uint32_t>(graphics_qf_index), surface)) {
        std::cerr << "Graphics queue doesn't support present (TODO: fix)\n";
        std::exit(EXIT_FAILURE);
    }
    
}

void Render::init_swapchain() {
    auto physical_device = instance.enumeratePhysicalDevices().front(); // May be dangerous (deterministic?)
    std::vector<vk::SurfaceFormatKHR> formats = physical_device.getSurfaceFormatsKHR(surface);
    vk::Format format = (formats[0].format == vk::Format::eUndefined) ? vk::Format::eB8G8R8A8Unorm : formats[0].format;
    vk::SurfaceCapabilitiesKHR surface_capabilities = physical_device.getSurfaceCapabilitiesKHR(surface);
    vk::Extent2D swapchain_extent;

    if(surface_capabilities.currentExtent.width == (std::numeric_limits<uint32_t>::max)()) {
        swapchain_extent.width = std::clamp(static_cast<uint32_t>(width), surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width);
        swapchain_extent.height = std::clamp(static_cast<uint32_t>(height), surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height);
    } else {
        swapchain_extent = surface_capabilities.currentExtent;
    }

    vk::PresentModeKHR swapchain_present_mode = vk::PresentModeKHR::eFifo;

    vk::SurfaceTransformFlagBitsKHR pre_transform = (surface_capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)
    ? vk::SurfaceTransformFlagBitsKHR::eIdentity : surface_capabilities.currentTransform;

    vk::CompositeAlphaFlagBitsKHR composite_alpha;
    // Order of preferences
    if(surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied) {
        composite_alpha = vk::CompositeAlphaFlagBitsKHR::ePreMultiplied; // Done by application
    } else if(surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied) {
        composite_alpha = vk::CompositeAlphaFlagBitsKHR::ePostMultiplied; // Done by compositor
    } else if(surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit) {
        composite_alpha = vk::CompositeAlphaFlagBitsKHR::eInherit;
    } else {
        composite_alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    }

    uint32_t image_count;
    if(surface_capabilities.maxImageCount == 0) {
        image_count = std::max(3u, surface_capabilities.minImageCount);
    } else {
        image_count = std::clamp(3u, surface_capabilities.minImageCount, surface_capabilities.maxImageCount);
    }

    vk::SwapchainCreateInfoKHR create_info(
        vk::SwapchainCreateFlagsKHR(),
        surface,
        image_count,
        format,
        vk::ColorSpaceKHR::eSrgbNonlinear,
        swapchain_extent,
        1,
        vk::ImageUsageFlagBits::eColorAttachment,
        vk::SharingMode::eExclusive,
        {},
        pre_transform,
        composite_alpha,
        swapchain_present_mode,
        true,
        nullptr
    );

    swapchain = device.createSwapchainKHR(create_info);
    swapchain_images = device.getSwapchainImagesKHR(swapchain);
    swapchain_image_views.reserve(swapchain_images.size());
    vk::ImageViewCreateInfo iv_create_info({}, {}, vk::ImageViewType::e2D, format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    for(auto image : swapchain_images) {
        iv_create_info.image = image;
        swapchain_image_views.push_back(device.createImageView(iv_create_info));
    }
}
