#include <vulkan/vulkan.h>
#include <stdlib.h>
#include <stdio.h>
#include <vulkan/vulkan_core.h>

#define ERR(func, msg)\
if(func != VK_SUCCESS) {\
  printf(msg);\
  exit(EXIT_FAILURE);\
}\

VkInstance instance;
VkPhysicalDevice physical_device;
VkDevice device;
size_t queue_index;
VkQueue queue;

void create_instance() {
  VkInstanceCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  if(vkCreateInstance(&info, NULL, &instance) != VK_SUCCESS) {
    printf("Could not create instance\n");
    exit(EXIT_FAILURE);
  }
}

void select_physical_device() {
  VkPhysicalDevice *physical_devices = NULL;
  uint32_t count;
  if(vkEnumeratePhysicalDevices(instance, &count, physical_devices) != VK_SUCCESS) {
    printf("Could not enumerate physical devices\n");
    exit(EXIT_FAILURE);
  }
  physical_devices = malloc(sizeof(VkPhysicalDevice) * count);
  if(vkEnumeratePhysicalDevices(instance, &count, physical_devices) != VK_SUCCESS) {
    printf("Could not enumerate physical devices\n");
    exit(EXIT_FAILURE);
  }
  if(count > 1) {
    printf("Multiple devices detected, selecting first one\n");
  }
  physical_device = physical_devices[0];
  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(physical_device, &properties);
  printf("Device name: %s\n", properties.deviceName);
  
  free(physical_devices);
}

void select_queue_family_index() {
  VkQueueFamilyProperties *properties = NULL;
  uint32_t count;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, properties);
  properties = malloc(sizeof(VkQueueFamilyProperties) * count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, properties);

  for(uint32_t i = 0; i != count; ++i) {
    if(properties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
      queue_index = i;
    }
  }

  free(properties);
}

void create_device() {
  float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info = {};
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.pQueuePriorities = &priority;
  queue_info.queueCount = 1;
  queue_info.flags = VK_QUEUE_COMPUTE_BIT;

  VkDeviceCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.queueCreateInfoCount = 1;
  info.pQueueCreateInfos = &queue_info;

  vkGetDeviceQueue(device, queue_index, 0, &queue);
}


void cleanup() {
  vkDestroyDevice(device, NULL);
  vkDestroyInstance(instance, NULL);
}

int main(void) {
  create_instance();
  select_physical_device();
  select_queue_family_index();

  cleanup();

  return 0;
}
