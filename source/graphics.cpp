#include "graphics.hpp"
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace graphics {

void checkResult(VkResult result, const char *action) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(action) + ": " + std::to_string(result));
}
Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void *data) {
    auto &context = internal::context;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    allocation.flags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo mapped{};
    Buffer out;
    checkResult(vmaCreateBuffer(context.allocator, &info, &allocation, &out.handle, &out.allocation,
                          &mapped),
          "Create buffer");
    out.mapped = mapped.pMappedData;
    if (data) {
        std::memcpy(out.mapped, data, static_cast<size_t>(size));
        VkResult result = vmaFlushAllocation(context.allocator, out.allocation, 0, size);
        if (result != VK_SUCCESS) {
            vmaDestroyBuffer(context.allocator, out.handle, out.allocation);
            checkResult(result, "Flush buffer");
        }
    }
    return out;
}
VkShaderModule loadShader(const char *filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error(std::string("Cannot open shader: ") + filename);
    auto length = file.tellg();
    if (length <= 0 || length % 4 != 0)
        throw std::runtime_error("Invalid SPIR-V length");
    std::vector<uint32_t> code(static_cast<size_t>(length) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char *>(code.data()), length);
    if (!file)
        throw std::runtime_error("Cannot read SPIR-V");
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size() * 4;
    info.pCode = code.data();
    VkShaderModule result{};
    checkResult(vkCreateShaderModule(internal::context.device, &info, nullptr, &result),
          "Create shader");
    return result;
}

void destroyBuffer(Buffer &buffer) {
    if (buffer.handle)
        vmaDestroyBuffer(internal::context.allocator, buffer.handle, buffer.allocation);
    buffer = {};
}

} // namespace graphics
