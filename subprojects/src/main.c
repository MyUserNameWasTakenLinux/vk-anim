#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

// int buffer[256];
#define NUM_ELEMENTS 256
#define ELEMENT_SIZE 4

#define SHADER_PATH "compute.spv"

#define ERR(func, msg)                                                         \
  if (func != VK_SUCCESS) {                                                    \
    printf(msg);                                                               \
    exit(EXIT_FAILURE);                                                        \
  }

typedef struct ShaderTuple {
  uint32_t *code;
  size_t code_size;
} ShaderTuple;

VkInstance instance;
VkPhysicalDevice physical_device;
VkDevice device;
uint32_t queue_index = UINT32_MAX;
VkQueue queue;
VkBuffer input_buffer;
VkBuffer output_buffer;
VkBuffer device_buffer;
VkDeviceMemory input_buffer_memory;
VkDeviceMemory output_buffer_memory;
VkDeviceMemory device_buffer_memory;
VkShaderModule shader_module;
VkDescriptorSetLayout descriptor_set_layout;
VkPipelineLayout pipeline_layout;
VkPipeline compute_pipeline;

ShaderTuple read_shader(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    printf("Could not open file\n");
    exit(EXIT_FAILURE);
  }

  fseek(f, 0, SEEK_END);
  size_t file_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  void *d = malloc(file_size);
  if (!d) {
    fclose(f);
    printf("Could not allocate enough memory for file\n");
    exit(EXIT_FAILURE);
  }

  size_t bytes_read = fread(d, 1, file_size, f);
  fclose(f);

  ShaderTuple tuple = {(uint32_t *)d, bytes_read};
  return tuple;
}

void create_instance() {
  VkInstanceCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  if (vkCreateInstance(&info, NULL, &instance) != VK_SUCCESS) {
    printf("Could not create instance\n");
    exit(EXIT_FAILURE);
  }
}

void select_physical_device() {
  VkPhysicalDevice *physical_devices = NULL;
  uint32_t count;
  if (vkEnumeratePhysicalDevices(instance, &count, physical_devices) !=
      VK_SUCCESS) {
    printf("Could not enumerate physical devices\n");
    exit(EXIT_FAILURE);
  }
  physical_devices = malloc(sizeof(VkPhysicalDevice) * count);
  if (vkEnumeratePhysicalDevices(instance, &count, physical_devices) !=
      VK_SUCCESS) {
    printf("Could not enumerate physical devices\n");
    exit(EXIT_FAILURE);
  }
  if (count > 1) {
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

  for (uint32_t i = 0; i != count; ++i) {
    if (properties[i].queueFlags &
        (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)) {
      queue_index = i;
      break;
    }
  }
  if (queue_index == UINT32_MAX) {
    printf("Failed to find appropriate queue family index\n");
    exit(EXIT_FAILURE);
  }

  free(properties);
}

void create_device() {
  float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info = {};
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.pQueuePriorities = &priority;
  queue_info.queueCount = 1;
  queue_info.queueFamilyIndex = queue_index;

  VkDeviceCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  info.queueCreateInfoCount = 1;
  info.pQueueCreateInfos = &queue_info;

  ERR(vkCreateDevice(physical_device, &info, NULL, &device),
      "Could not create device\n")

  vkGetDeviceQueue(device, queue_index, 0, &queue);
}

void create_buffers() {
  VkBufferCreateInfo i_create_info = {};
  i_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  i_create_info.size = ELEMENT_SIZE * NUM_ELEMENTS;
  i_create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  i_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  i_create_info.queueFamilyIndexCount = 1;
  i_create_info.pQueueFamilyIndices = &queue_index;
  ERR(vkCreateBuffer(device, &i_create_info, NULL, &input_buffer),
      "Could not create input buffer\n")

  VkBufferCreateInfo o_create_info = {};
  o_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  o_create_info.size = ELEMENT_SIZE * NUM_ELEMENTS;
  o_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  o_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  o_create_info.queueFamilyIndexCount = 1;
  o_create_info.pQueueFamilyIndices = &queue_index;
  ERR(vkCreateBuffer(device, &o_create_info, NULL, &output_buffer),
      "Could not create output buffer\n")

  VkBufferCreateInfo d_create_info = {};
  d_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  d_create_info.size = ELEMENT_SIZE * NUM_ELEMENTS;
  d_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  d_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  d_create_info.queueFamilyIndexCount = 1;
  d_create_info.pQueueFamilyIndices = &queue_index;
  ERR(vkCreateBuffer(device, &d_create_info, NULL, &device_buffer),
      "Could not create device buffer\n")
}

void allocate_memory() {
  VkMemoryRequirements i_buffer_requirements;
  vkGetBufferMemoryRequirements(device, input_buffer, &i_buffer_requirements);

  VkPhysicalDeviceMemoryProperties memory_properties;
  vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

  uint32_t input_mem_index = UINT32_MAX;
  for (uint32_t i = 0; i != memory_properties.memoryTypeCount; i++) {
    const uint32_t bit_select = (1 << i);
    if (bit_select & i_buffer_requirements.memoryTypeBits &&
        memory_properties.memoryTypes[i].propertyFlags &
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      input_mem_index = i;
      break;
    }
  }
  if (input_mem_index == UINT32_MAX) {
    printf("Could not find appropriate memory index for input buffer\n");
    exit(EXIT_FAILURE);
  }

  VkMemoryRequirements o_buffer_requirements;
  vkGetBufferMemoryRequirements(device, output_buffer, &o_buffer_requirements);

  uint32_t output_mem_index = UINT32_MAX;
  for (uint32_t i = 0; i != memory_properties.memoryTypeCount; i++) {
    const uint32_t bit_select = (1 << i);
    if (bit_select & o_buffer_requirements.memoryTypeBits &&
        memory_properties.memoryTypes[i].propertyFlags &
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      output_mem_index = i;
      break;
    }
  }
  if (output_mem_index == UINT32_MAX) {
    printf("Could not find appropriate memory for output buffer\n");
    exit(EXIT_FAILURE);
  }

  VkMemoryRequirements d_buffer_requirements;
  vkGetBufferMemoryRequirements(device, device_buffer, &d_buffer_requirements);

  uint32_t device_mem_index = UINT32_MAX;
  for (uint32_t i = 0; i != memory_properties.memoryTypeCount; i++) {
    const uint32_t bit_select = (1 << i);
    if (bit_select & d_buffer_requirements.memoryTypeBits &&
        memory_properties.memoryTypes[i].propertyFlags &
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      device_mem_index = i;
      break;
    }
  }
  if (device_mem_index == UINT32_MAX) {
    printf("Could not find appropriate memory for device buffer\n");
    exit(EXIT_FAILURE);
  }

  VkMemoryAllocateInfo i_buffer_info = {};
  i_buffer_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  i_buffer_info.memoryTypeIndex = input_mem_index;
  i_buffer_info.allocationSize = i_buffer_requirements.size;
  ERR(vkAllocateMemory(device, &i_buffer_info, NULL, &input_buffer_memory),
      "Could not allocate memory for input buffer\n")

  VkMemoryAllocateInfo o_buffer_info = {};
  o_buffer_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  o_buffer_info.memoryTypeIndex = output_mem_index;
  o_buffer_info.allocationSize = o_buffer_requirements.size;
  ERR(vkAllocateMemory(device, &o_buffer_info, NULL, &output_buffer_memory),
      "Could not allocate memory for output buffer\n")

  VkMemoryAllocateInfo d_buffer_info = {};
  d_buffer_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  d_buffer_info.memoryTypeIndex = device_mem_index;
  d_buffer_info.allocationSize = d_buffer_requirements.size;
  ERR(vkAllocateMemory(device, &d_buffer_info, NULL, &device_buffer_memory),
      "Could not allocate memory for device buffer\n")
}

void bind_memory() {
  ERR(vkBindBufferMemory(device, input_buffer, input_buffer_memory, 0),
      "Could not bind memory for input buffer\n")
  ERR(vkBindBufferMemory(device, output_buffer, output_buffer_memory, 0),
      "Could not bind memory for output buffer\n")
  ERR(vkBindBufferMemory(device, device_buffer, device_buffer_memory, 0),
      "Could not bind memory for device buffer\n")
}

void create_shader_module() {
  ShaderTuple t = read_shader(SHADER_PATH);

  VkShaderModuleCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = t.code_size;
  info.pCode = t.code;

  ERR(vkCreateShaderModule(device, &info, NULL, &shader_module),
      "Could not create shader module\n");
  free(t.code);
}

void create_descriptor_set_layout() {
  VkDescriptorSetLayoutBinding dset_layout = {};
  dset_layout.binding = 0;
  dset_layout.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  dset_layout.descriptorCount = 1;
  dset_layout.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  VkDescriptorSetLayoutCreateInfo layout_info = {};
  layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_info.bindingCount = 1;
  layout_info.pBindings = &dset_layout;

  ERR(vkCreateDescriptorSetLayout(device, &layout_info, NULL,
                                  &descriptor_set_layout),
      "Could not create descriptor set layout\n")
}

void allocate_descriptor_set() {
  printf("TODO\n");
  exit(EXIT_FAILURE);
}

void create_compute_pipeline() {
  VkPipelineLayoutCreateInfo pipeline_layout_info = {};
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.setLayoutCount = 1;
  pipeline_layout_info.pSetLayouts = &descriptor_set_layout;

  ERR(vkCreatePipelineLayout(device, &pipeline_layout_info, NULL,
                             &pipeline_layout),
      "Could not create pipeline layout\n");

  VkPipelineShaderStageCreateInfo shader_stage_info = {};
  shader_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shader_stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  shader_stage_info.module = shader_module;
  shader_stage_info.pName = "main"; // Careful hardcoded

  VkComputePipelineCreateInfo compute_pipeline_info = {};
  compute_pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  compute_pipeline_info.stage = shader_stage_info;
  compute_pipeline_info.layout = pipeline_layout;

  ERR(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1,
                               &compute_pipeline_info, NULL, &compute_pipeline),
      "Could not create compute pipeline\n")
}

void cleanup() {
  vkDestroyDescriptorSetLayout(device, descriptor_set_layout, NULL);
  vkDestroyShaderModule(device, shader_module, NULL);
  vkFreeMemory(device, input_buffer_memory, NULL);
  vkFreeMemory(device, output_buffer_memory, NULL);
  vkFreeMemory(device, device_buffer_memory, NULL);
  vkDestroyBuffer(device, input_buffer, NULL);
  vkDestroyBuffer(device, output_buffer, NULL);
  vkDestroyBuffer(device, device_buffer, NULL);
  vkDestroyDevice(device, NULL);
  vkDestroyInstance(instance, NULL);
}

int main(void) {
  create_instance();
  select_physical_device();
  select_queue_family_index();
  create_device();
  create_buffers();
  allocate_memory();
  bind_memory();
  create_shader_module();
  create_descriptor_set_layout();
  allocate_descriptor_set();
  create_compute_pipeline();

  cleanup();

  return 0;
}
