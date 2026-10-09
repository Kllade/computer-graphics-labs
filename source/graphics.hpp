#pragma once
#include "graphics_internal.hpp"

// Две повторяющиеся операции Vulkan. Устройство и render pass предоставляет шаблон.
namespace graphics {

struct Buffer {
    VkBuffer handle{};
    VmaAllocation allocation{};
    void *mapped{};
};

void checkResult(VkResult result, const char *action);
Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void *data = nullptr);
void destroyBuffer(Buffer &buffer);
VkShaderModule loadShader(const char *filename);

} // namespace graphics
