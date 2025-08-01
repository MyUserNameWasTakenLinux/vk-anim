#include "render.h"
#include <algorithm>
#include <limits>
#include <glm/glm.hpp>
#include <fstream>

const std::vector<const char*> validation_layers = {
    "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> device_extensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

const std::string vertex_shader_file = "test.vert.spv";
const std::string fragment_shader_file = "test.frag.spv";

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

vk::ShaderModule load_SPIRV_shader(const std::string& filename, vk::Device& device) {
    std::vector<uint32_t> shader_code;
    std::ifstream is(filename, std::ios::binary | std::ios::ate);

    if(is.is_open()) {
        std::streamsize size = is.tellg();
        is.seekg(0, std::ios::beg);

        if(size % 4 != 0) {
            std::cerr << "Shader file size not a multiple of 4\n";
            std::exit(EXIT_FAILURE);
        }

        shader_code.resize(size / 4);
        is.read(reinterpret_cast<char*>(shader_code.data()), size);
        is.close();
    }

    if(!shader_code.empty()) {
        vk::ShaderModuleCreateInfo create_info(vk::ShaderModuleCreateFlags(), shader_code.size() * 4, shader_code.data());
        vk::ShaderModule shader_module = device.createShaderModule(create_info);

        return shader_module;
    } else {
        std::cerr << "Something went wrong with shaders\n";
        std::exit(EXIT_FAILURE);
    }
}

Render::Render(int width, int height, std::string name) : width(width), height(height), name(name) {
    init_window();
    init_vulkan();
    init_swapchain();
    init_depth_buffer();
    init_uniform_buffer();
    init_pipeline();
    init_vertex_buffer();
    init_command_buffer();
}

void Render::add_vobject(VObject v) {
    if(free_vertex_mem_index >= 32 * 2048) {
        std::cerr << "Increase vertex buffer size\n";
        std::exit(EXIT_FAILURE);
    }
    auto transfer_size = sizeof(Vertex) * v.vertices.size();
    void* data = device.mapMemory(vertex_buffer.memory, free_vertex_mem_index, transfer_size); // TODO: update free_vertex_mem_index
    memcpy(data, v.vertices.data(), transfer_size);
    device.unmapMemory(vertex_buffer.memory);

    render_objects.push_back(RenderObject(v, free_vertex_mem_index / sizeof(Vertex)));
    free_vertex_mem_index += v.vertices.size();
}

void Render::loop() {
    image_acquired_semaphore = device.createSemaphore(vk::SemaphoreCreateInfo());
    draw_fence = device.createFence(vk::FenceCreateInfo());
    
    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        vk::ResultValue<uint32_t> next_image = device.acquireNextImageKHR(swapchain.handle, 100000000, image_acquired_semaphore, nullptr);
        if(next_image.result != vk::Result::eSuccess || next_image.value >= swapchain.image_views.size()) {
            std::cerr << "Error with acquiring next image\n";
            std::exit(EXIT_FAILURE);
        }
        uint32_t image_index = next_image.value;

        command_buffer.begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlags()));

        std::array<vk::ClearValue, 2> clear_values;
        clear_values[0].color = vk::ClearColorValue(0.5f, 0.2f, 0.2f, 0.2f);
        clear_values[1].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);

        vk::RenderingAttachmentInfo color_attachment {};
        color_attachment.imageView = swapchain.image_views[image_index];
        color_attachment.imageLayout = vk::ImageLayout::eAttachmentOptimal;
        color_attachment.loadOp = vk::AttachmentLoadOp::eClear;
        color_attachment.storeOp = vk::AttachmentStoreOp::eStore;
        color_attachment.clearValue = clear_values[0];

        vk::RenderingAttachmentInfo depth_attachment {};
        depth_attachment.imageView = depth_buffer.image_view;
        depth_attachment.imageLayout = vk::ImageLayout::eDepthAttachmentOptimal;
        depth_attachment.loadOp = vk::AttachmentLoadOp::eClear;
        depth_attachment.storeOp = vk::AttachmentStoreOp::eStore;
        depth_attachment.clearValue = clear_values[1];
        

        vk::RenderingInfo rendering_info {};
        rendering_info.renderArea = vk::Rect2D({0, 0}, swapchain.extent);
        rendering_info.layerCount = 1;
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachments = &color_attachment;
        rendering_info.pDepthAttachment = &depth_attachment;

        vk::ImageMemoryBarrier color_barrier {};
        color_barrier.oldLayout = vk::ImageLayout::eUndefined;
        color_barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
        color_barrier.srcAccessMask = {};
        color_barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        color_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        color_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        color_barrier.image = swapchain.images[image_index];
        color_barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        color_barrier.subresourceRange.baseMipLevel = 0;
        color_barrier.subresourceRange.levelCount = 1;
        color_barrier.subresourceRange.baseArrayLayer = 0;
        color_barrier.subresourceRange.layerCount = 1;

        vk::ImageMemoryBarrier depth_barrier {};
        depth_barrier.oldLayout = vk::ImageLayout::eUndefined;
        depth_barrier.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
        depth_barrier.srcAccessMask = {};
        depth_barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
        depth_barrier.image = depth_buffer.image;
        depth_barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
        depth_barrier.subresourceRange.baseMipLevel = 0;
        depth_barrier.subresourceRange.levelCount = 1;
        depth_barrier.subresourceRange.baseArrayLayer = 0;
        depth_barrier.subresourceRange.layerCount = 1;

        std::array<vk::ImageMemoryBarrier, 2> barriers = {color_barrier, depth_barrier};

        command_buffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
            {},
            nullptr,
            nullptr,
            barriers
        );

        command_buffer.beginRendering(rendering_info);

        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, descriptor_set, nullptr);
        command_buffer.bindVertexBuffers(0, vertex_buffer.buffer, {0});
        command_buffer.setViewport(
            0, 
            vk::Viewport(0.0f, 0.0f, static_cast<float>(swapchain.extent.width), static_cast<float>(swapchain.extent.height), 0.0f, 1.0f)
        );
        command_buffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapchain.extent));

        for(auto ro : render_objects) {
            command_buffer.draw(ro.vobject.vertices.size(), 1, ro.first_vertex, 0);
        }

        command_buffer.endRendering();

        vk::ImageMemoryBarrier present_barrier {};
        present_barrier.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
        present_barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
        present_barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
        present_barrier.dstAccessMask = {};
        present_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        present_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        present_barrier.image = swapchain.images[image_index];
        present_barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        present_barrier.subresourceRange.baseMipLevel = 0;
        present_barrier.subresourceRange.levelCount = 1;
        present_barrier.subresourceRange.baseArrayLayer = 0;
        present_barrier.subresourceRange.layerCount = 1;

        command_buffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::PipelineStageFlagBits::eBottomOfPipe,
            {},
            nullptr,
            nullptr,
            present_barrier
        );

        command_buffer.end();

        vk::PipelineStageFlags wait_dst_stage_mask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
        vk::SubmitInfo submit_info(image_acquired_semaphore, wait_dst_stage_mask, command_buffer);
        graphics_queue.submit(submit_info, draw_fence);

        while(vk::Result::eTimeout == device.waitForFences(draw_fence, VK_TRUE, 100000000))
            ;
        device.resetFences(draw_fence);
        vk::Result result = graphics_queue.presentKHR(vk::PresentInfoKHR({}, swapchain.handle, image_index));
        if(result != vk::Result::eSuccess) {
            std::cout << "Image present was not a success\n";
        }

        command_buffer.reset();
    }
}

Render::~Render() {

    device.destroyFence(draw_fence);
    device.destroySemaphore(image_acquired_semaphore);
    device.destroyCommandPool(command_pool);
    device.destroyBuffer(vertex_buffer.buffer);
    device.freeMemory(vertex_buffer.memory);
    device.destroyPipeline(pipeline);
    device.destroyPipelineLayout(pipeline_layout);
    device.freeDescriptorSets(descriptor_pool, descriptor_set);
    device.destroyDescriptorPool(descriptor_pool);
    device.destroyDescriptorSetLayout(descriptor_set_layout);
    device.destroyBuffer(uniform_buffer.buffer);
    device.freeMemory(uniform_buffer.memory);
    device.destroyImageView(depth_buffer.image_view);
    device.destroyImage(depth_buffer.image);
    device.freeMemory(depth_buffer.memory);
    for(auto& iv : swapchain.image_views) {
        device.destroyImageView(iv);
    }
    device.destroySwapchainKHR(swapchain.handle);
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
    vk::PhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features = {};
    dynamic_rendering_features.setDynamicRendering(VK_TRUE);
    auto device_info = vk::DeviceCreateInfo(vk::DeviceCreateFlags(), queue_info, {}, device_extensions);
    device_info.pNext = &dynamic_rendering_features;
    device = phys_device.createDevice(device_info);

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
    swapchain.format = (formats[0].format == vk::Format::eUndefined) ? vk::Format::eB8G8R8A8Unorm : formats[0].format;
    vk::SurfaceCapabilitiesKHR surface_capabilities = physical_device.getSurfaceCapabilitiesKHR(surface);

    if(surface_capabilities.currentExtent.width == (std::numeric_limits<uint32_t>::max)()) {
        swapchain.extent.width = std::clamp(static_cast<uint32_t>(width), surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width);
        swapchain.extent.height = std::clamp(static_cast<uint32_t>(height), surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height);
    } else {
        swapchain.extent = surface_capabilities.currentExtent;
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
        swapchain.format,
        vk::ColorSpaceKHR::eSrgbNonlinear,
        swapchain.extent,
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

    swapchain.handle = device.createSwapchainKHR(create_info);
    swapchain.images = device.getSwapchainImagesKHR(swapchain.handle);
    swapchain.image_views.reserve(swapchain.images.size());
    vk::ImageViewCreateInfo iv_create_info({}, {}, vk::ImageViewType::e2D, swapchain.format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    for(auto image : swapchain.images) {
        iv_create_info.image = image;
        swapchain.image_views.push_back(device.createImageView(iv_create_info));
    }
}

void Render::init_depth_buffer() {
    auto physical_device = instance.enumeratePhysicalDevices().front(); // May be dangerous (deterministic?)
    vk::Format depth_format = vk::Format::eD16Unorm;
    vk::FormatProperties format_properties = physical_device.getFormatProperties(depth_format);

    vk::ImageTiling tiling;
    if(format_properties.linearTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
        tiling = vk::ImageTiling::eLinear;
    } else if(format_properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) {
        tiling = vk::ImageTiling::eOptimal;
    } else {
        std::cerr << "DepthStencilAttachment not supported for format D16Unorm\n";
        std::exit(EXIT_FAILURE);
    }

    vk::ImageCreateInfo create_info(
        vk::ImageCreateFlags(),
        vk::ImageType::e2D,
        depth_format,
        vk::Extent3D(width, height, 1),
        1,
        1,
        vk::SampleCountFlagBits::e1,
        tiling,
        vk::ImageUsageFlagBits::eDepthStencilAttachment
    );

    depth_buffer.image = device.createImage(create_info);
    vk::PhysicalDeviceMemoryProperties mem_prop = physical_device.getMemoryProperties();
    vk::MemoryRequirements mem_reqs = device.getImageMemoryRequirements(depth_buffer.image);
    uint32_t type_bits = mem_reqs.memoryTypeBits;
    uint32_t type_index = std::numeric_limits<uint32_t>::max();

    for(uint32_t i = 0; i < mem_prop.memoryTypeCount; ++i) {
        if((type_bits & 1) && (mem_prop.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)) {
            type_index = i;
            break;
        }
        type_bits >>= 1;
    }

    if(type_index == std::numeric_limits<uint32_t>::max()) {
        std::cerr << "Memory type not found\n";
        std::exit(EXIT_FAILURE);
    }

    depth_buffer.memory = device.allocateMemory(vk::MemoryAllocateInfo(mem_reqs.size, type_index));
    device.bindImageMemory(depth_buffer.image, depth_buffer.memory, 0);

    depth_buffer.image_view = device.createImageView(vk::ImageViewCreateInfo(
        vk::ImageViewCreateFlags(), depth_buffer.image, vk::ImageViewType::e2D, depth_format, {}, {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}
    ));

}

// May need multiple of these later on to avoid data overwrite with multiple frames in flight
void Render::init_uniform_buffer() {
    auto physical_device = instance.enumeratePhysicalDevices().front(); // May be dangerous (deterministic?)
    uniform_buffer.size = sizeof(glm::mat4x4) * 3;
    uniform_buffer.buffer = device.createBuffer(vk::BufferCreateInfo(vk::BufferCreateFlags(), uniform_buffer.size, vk::BufferUsageFlagBits::eUniformBuffer));

    vk::MemoryRequirements mem_reqs = device.getBufferMemoryRequirements(uniform_buffer.buffer);
    vk::PhysicalDeviceMemoryProperties mem_props = physical_device.getMemoryProperties();
    uint32_t type_bits = mem_reqs.memoryTypeBits;
    uint32_t type_index = std::numeric_limits<uint32_t>::max();

    for(uint32_t i = 0; i != mem_props.memoryTypeCount; ++i) {
        if(
            (type_bits & 1) &&
            (mem_props.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible) &&
            (mem_props.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent)
        ) {
            type_index = i;
            break;
        }

        type_bits >>= 1;
    }

    if(type_index == std::numeric_limits<uint32_t>::max()) {
        std::cerr << "Memory type not found\n";
        std::exit(EXIT_FAILURE);
    }

    uniform_buffer.memory = device.allocateMemory(vk::MemoryAllocateInfo(mem_reqs.size, type_index));
    device.bindBufferMemory(uniform_buffer.buffer, uniform_buffer.memory, 0);
}

void Render::init_pipeline() {
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    bindings.push_back(vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex));
    vk::DescriptorSetLayoutCreateInfo create_info(vk::DescriptorSetLayoutCreateFlags(), bindings);
    descriptor_set_layout = device.createDescriptorSetLayout(create_info);

    vk::DescriptorPoolSize pool_size(vk::DescriptorType::eUniformBuffer, 1);
    descriptor_pool = device.createDescriptorPool(vk::DescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, pool_size));

    vk::DescriptorSetAllocateInfo allocate_info(descriptor_pool, descriptor_set_layout);
    descriptor_set = device.allocateDescriptorSets(allocate_info).front();

    vk::DescriptorBufferInfo descriptor_buffer_info(uniform_buffer.buffer, 0, uniform_buffer.size);
    vk::WriteDescriptorSet write_descriptor_set(descriptor_set, 0, 0, vk::DescriptorType::eUniformBuffer, {}, descriptor_buffer_info);
    device.updateDescriptorSets(write_descriptor_set, nullptr);

    pipeline_layout = device.createPipelineLayout(vk::PipelineLayoutCreateInfo(vk::PipelineLayoutCreateFlags(), descriptor_set_layout));

    vk::ShaderModule vertex_shader_module = load_SPIRV_shader(vertex_shader_file, device);
    vk::ShaderModule fragment_shader_module = load_SPIRV_shader(fragment_shader_file, device);

    std::array<vk::PipelineShaderStageCreateInfo, 2> pipeline_shader_stage_create_infos = {
        vk::PipelineShaderStageCreateInfo(vk::PipelineShaderStageCreateFlags(), vk::ShaderStageFlagBits::eVertex, vertex_shader_module, "main"),
        vk::PipelineShaderStageCreateInfo(vk::PipelineShaderStageCreateFlags(), vk::ShaderStageFlagBits::eFragment, fragment_shader_module, "main"),
    };

    vk::VertexInputBindingDescription vertex_input_binding_description(0, sizeof(Vertex));
    std::array<vk::VertexInputAttributeDescription, 2> vertex_input_attribute_descriptions = {
        vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32A32Sfloat, 0),
        vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32A32Sfloat, 16)
    };

    vk::PipelineVertexInputStateCreateInfo pipeline_vertex_input_state_create_info(vk::PipelineVertexInputStateCreateFlags(), vertex_input_binding_description, vertex_input_attribute_descriptions);
    vk::PipelineInputAssemblyStateCreateInfo pipeline_input_assembly_state_create_info(vk::PipelineInputAssemblyStateCreateFlags(), vk::PrimitiveTopology::eLineStrip);

    vk::PipelineViewportStateCreateInfo pipeline_viewport_state_create_info(vk::PipelineViewportStateCreateFlags(), 1, nullptr, 1, nullptr);

    vk::PipelineRasterizationStateCreateInfo pipeline_rasterization_state_create_info(
        vk::PipelineRasterizationStateCreateFlags(),
        false,
        false,
        vk::PolygonMode::eFill,
        vk::CullModeFlagBits::eBack,
        vk::FrontFace::eClockwise,
        false,
        0.0f,
        0.0f,
        0.0f,
        1.0f
    );

    vk::PipelineMultisampleStateCreateInfo pipeline_multisample_state_create_info(vk::PipelineMultisampleStateCreateFlags(), vk::SampleCountFlagBits::e1);

    vk::StencilOpState stencil_op_state(vk::StencilOp::eKeep, vk::StencilOp::eKeep, vk::StencilOp::eKeep, vk::CompareOp::eAlways);
    vk::PipelineDepthStencilStateCreateInfo pipeline_depth_stencil_state_create_info(
        vk::PipelineDepthStencilStateCreateFlags(),
        true,
        true,
        vk::CompareOp::eLessOrEqual,
        false,
        false,
        stencil_op_state,
        stencil_op_state
    );

    vk::ColorComponentFlags color_component_flags(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
    vk::PipelineColorBlendAttachmentState pipeline_color_blend_attachment_state(
        false,
        vk::BlendFactor::eZero,
        vk::BlendFactor::eZero,
        vk::BlendOp::eAdd,
        vk::BlendFactor::eZero,
        vk::BlendFactor::eZero,
        vk::BlendOp::eAdd,
        color_component_flags
    );

    vk::PipelineColorBlendStateCreateInfo pipeline_color_blend_state_create_info(
        vk::PipelineColorBlendStateCreateFlags(),
        false,
        vk::LogicOp::eNoOp,
        pipeline_color_blend_attachment_state,
        {{1.0f, 1.0f, 1.0f, 1.0f}}
    );

    std::array<vk::DynamicState, 2> dynamic_states = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
    vk::PipelineDynamicStateCreateInfo pipeline_dynamic_state_create_info(vk::PipelineDynamicStateCreateFlags(), dynamic_states);

    vk::PipelineRenderingCreateInfo pipeline_rendering_create_info {};
    pipeline_rendering_create_info.colorAttachmentCount = 1;
    pipeline_rendering_create_info.pColorAttachmentFormats = &swapchain.format;
    pipeline_rendering_create_info.depthAttachmentFormat = vk::Format::eD16Unorm;

    vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info(
        vk::PipelineCreateFlags(),
        pipeline_shader_stage_create_infos,
        &pipeline_vertex_input_state_create_info,
        &pipeline_input_assembly_state_create_info,
        nullptr,
        &pipeline_viewport_state_create_info,
        &pipeline_rasterization_state_create_info,
        &pipeline_multisample_state_create_info,
        &pipeline_depth_stencil_state_create_info,
        &pipeline_color_blend_state_create_info,
        &pipeline_dynamic_state_create_info,
        pipeline_layout
    );
    graphics_pipeline_create_info.pNext = &pipeline_rendering_create_info;
    
    vk::Result result;
    std::tie(result, pipeline) = device.createGraphicsPipeline(nullptr, graphics_pipeline_create_info);
    switch(result) {
        case vk::Result::eSuccess:
            break;
        case vk::Result::ePipelineCompileRequired:
            break;
        default:
            std::cerr << "Something went wrong with graphics pipeline creation\n";
            std::exit(EXIT_FAILURE);
    }

    device.destroyShaderModule(vertex_shader_module);
    device.destroyShaderModule(fragment_shader_module);
}

void Render::init_vertex_buffer() {
    auto physical_device = instance.enumeratePhysicalDevices().front(); // May be dangerous (deterministic?)
    vk::DeviceSize buffer_size = vertex_buffer.size;
    vk::BufferCreateInfo buffer_info(vk::BufferCreateFlags(), buffer_size, vk::BufferUsageFlagBits::eVertexBuffer);
    vertex_buffer.buffer = device.createBuffer(buffer_info);

    vk::MemoryRequirements mem_reqs = device.getBufferMemoryRequirements(vertex_buffer.buffer);
    
    vk::PhysicalDeviceMemoryProperties mem_props = physical_device.getMemoryProperties();
    uint32_t type_bits = mem_reqs.memoryTypeBits;
    uint32_t type_index = std::numeric_limits<uint32_t>::max();

    for(uint32_t i = 0; i != mem_props.memoryTypeCount; ++i) {
        // Running on unified memory architecture so these flags are fine even in the case of vertex buffers
        if(
            (type_bits & 1) &&
            (mem_props.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible) &&
            (mem_props.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent)
        ) {
            type_index = i;
            break;
        }

        type_bits >>= 1;
    }

    if(type_index == std::numeric_limits<uint32_t>::max()) {
        std::cerr << "Memory type not found\n";
        std::exit(EXIT_FAILURE);
    }

    vertex_buffer.memory = device.allocateMemory(vk::MemoryAllocateInfo(mem_reqs.size, type_index));
    device.bindBufferMemory(vertex_buffer.buffer, vertex_buffer.memory, 0);
}

void Render::init_command_buffer() {
    command_pool = device.createCommandPool(vk::CommandPoolCreateInfo(vk::CommandPoolCreateFlagBits::eResetCommandBuffer, graphics_qf_index));
    command_buffer = device.allocateCommandBuffers(vk::CommandBufferAllocateInfo(command_pool, vk::CommandBufferLevel::ePrimary, 1)).front();
}