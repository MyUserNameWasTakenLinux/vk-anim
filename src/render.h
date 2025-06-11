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
vk::SwapchainKHR swapchain {};
std::vector<vk::Image> swapchain_images {}; // Image memory handled by the swapchain
std::vector<vk::ImageView> swapchain_image_views {};

public:
    Render(int width, int height, std::string name);
    ~Render();

private:
    void init_window();
    void init_vulkan();
    void init_swapchain();
};

class Scene {

};