#pragma once

#include "vobject.h"
#include <string>
#include <iostream>
#include <vector>
#include <vulkan/vulkan.hpp>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

class Render {
private:
int width, height;
std::string name;
GLFWwindow* window;

vk::Instance instance {};
vk::Device device {};
size_t graphics_qf_index {};
vk::Queue graphics_queue {};
vk::SurfaceKHR surface {};

struct {
    vk::SwapchainKHR handle;
    std::vector<vk::Image> images {}; // Image memory handled by the swapchain
    std::vector<vk::ImageView> image_views {};
    vk::Format format;
    vk::Extent2D extent;
} swapchain;

struct {
    vk::Image image {};
    vk::DeviceMemory memory {};
    vk::ImageView image_view {};
} depth_buffer;

struct {
    vk::Buffer buffer {};
    vk::DeviceMemory memory {};
    uint32_t size; // In bytes
} uniform_buffer;

vk::DescriptorSetLayout descriptor_set_layout;
vk::DescriptorPool descriptor_pool;
vk::DescriptorSet descriptor_set;

vk::PipelineLayout pipeline_layout;
vk::Pipeline pipeline;

vk::CommandPool command_pool;
vk::CommandBuffer command_buffer;

vk::Fence draw_fence;
vk::Semaphore image_acquired_semaphore;

struct RenderObject {
    VObject vobject;
    uint32_t first_vertex;

    RenderObject(VObject v, uint32_t i) : vobject(v), first_vertex(i) {}
    ~RenderObject() {} 
};

uint32_t free_vertex_mem_index = 0; // In bytes

std::vector<RenderObject> render_objects;

struct {
    const uint32_t size = 32 * 2048; // 2048 Vertices
    vk::Buffer buffer {};
    vk::DeviceMemory memory {};
} vertex_buffer;

public:
    Render(int width, int height, std::string name);
    void add_vobject(VObject v);
    void loop();
    ~Render();

private:
    void init_window();
    void init_vulkan();
    void init_swapchain();
    void init_depth_buffer();
    void init_uniform_buffer();
    void init_pipeline();
    void init_vertex_buffer();
    void init_command_buffer();
};
