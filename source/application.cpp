#include "application.hpp"
#include "icosahedron.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <imgui.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace application {
namespace {

// Только геометрия, один конвейер и одна матрица для одного объекта.
struct Buffer {
    VkBuffer handle{};
    VmaAllocation allocation{};
};
Buffer vertexBuffer, indexBuffer;
VkPipelineLayout pipelineLayout{};
VkPipeline pipeline{};
VkShaderModule vertexShader{}, fragmentShader{};
static_assert(sizeof(glm::mat4) == 64);

void checkResult(VkResult result, const char *action) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(action) + ": " + std::to_string(result));
}

Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void *data) {
    auto &context = graphics::internal::context;
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
    std::memcpy(mapped.pMappedData, data, static_cast<size_t>(size));
    VkResult result = vmaFlushAllocation(context.allocator, out.allocation, 0, size);
    if (result != VK_SUCCESS) {
        vmaDestroyBuffer(context.allocator, out.handle, out.allocation);
        checkResult(result, "Flush buffer");
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
    checkResult(vkCreateShaderModule(graphics::internal::context.device, &info, nullptr, &result),
                "Create shader");
    return result;
}

void destroyBuffer(Buffer &buffer) {
    if (buffer.handle)
        vmaDestroyBuffer(graphics::internal::context.allocator, buffer.handle, buffer.allocation);
    buffer = {};
}

void createGraphicsPipeline() {
    auto &context = graphics::internal::context;
    // 1. Вершинный и фрагментный шейдеры.
    vertexShader = loadShader("shaders/icosahedron.vert.spv");
    fragmentShader = loadShader("shaders/icosahedron.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto &stage : stages) {
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexShader;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentShader;
    // 2. Вершина — три float: x, y, z.
    VkVertexInputBindingDescription binding{0, sizeof(glm::vec3),
                                            VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription position{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
    VkPipelineVertexInputStateCreateInfo input{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 1;
    input.pVertexAttributeDescriptions = &position;
    // 3. Сборка треугольников, viewport/scissor и растеризация.
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    // Окно фиксировано: viewport и scissor задаются один раз.
    const VkViewport area{0, 0, static_cast<float>(context.swapchain_extent.width),
                          static_cast<float>(context.swapchain_extent.height), 0, 1};
    const VkRect2D scissor{{0, 0}, context.swapchain_extent};
    viewport.viewportCount = 1;
    viewport.pViewports = &area;
    viewport.scissorCount = 1;
    viewport.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    // 4. Глубина отсеивает скрытые поверхности; смешивание цветов выключено.
    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    // 5. Соединить настройки с pipeline layout и render pass преподавателя.
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.layout = pipelineLayout;
    info.renderPass = context.render_pass;
    checkResult(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
                "Create graphics pipeline");
}

} // namespace

bool initialize() {
    try {
        // Создаем буферы с вершинами и треугольниками икосаэдра.
        vertexBuffer = createBuffer(sizeof(icosahedron::vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    icosahedron::vertices.data());
        indexBuffer = createBuffer(sizeof(icosahedron::indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                   icosahedron::indices.data());

        // Шейдер получает одну матрицу через push constants (без UBO и дескрипторов).
        VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4)};
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &range;
        checkResult(vkCreatePipelineLayout(graphics::internal::context.device, &layout, nullptr,
                                          &pipelineLayout), "Create pipeline layout");
        createGraphicsPipeline();
        return true;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        shutdown();
        return false;
    }
}

void shutdown() {
    const auto device = graphics::internal::context.device;
    vkDeviceWaitIdle(device);
    if (pipeline)
        vkDestroyPipeline(device, pipeline, nullptr);
    if (pipelineLayout)
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    if (vertexShader)
        vkDestroyShaderModule(device, vertexShader, nullptr);
    if (fragmentShader)
        vkDestroyShaderModule(device, fragmentShader, nullptr);
    vertexShader = fragmentShader = VK_NULL_HANDLE;
    destroyBuffer(indexBuffer);
    destroyBuffer(vertexBuffer);
    pipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
}

void update([[maybe_unused]] double time) {
    // ImGui остается частью приложения; дополнительных настроек фигуры нет.
    ImGui::Begin("Lab 1");
    ImGui::TextUnformatted("Icosahedron / Variant 11");
    ImGui::End();
}

void render(const graphics::internal::FrameData &fd) {
    const auto &context = graphics::internal::context;

    // Фиксированная камера и перспективная проекция. Модель стоит в начале координат.
    const float aspect = static_cast<float>(context.swapchain_extent.width) /
                         context.swapchain_extent.height;
    const glm::mat4 model(1.0f);
    const glm::mat4 view = glm::lookAtRH(glm::vec3(2.6f, 1.6f, 4.0f), glm::vec3(0),
                                       glm::vec3(0, 1, 0));
    glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    projection[1][1] *= -1; // Коррекция Y для viewport Vulkan с положительной высотой.
    const glm::mat4 mvp = projection * view * model;

    // Начинаем запись команд и очищаем цвет и глубину.
    checkResult(vkResetCommandBuffer(fd.command_buffer, 0), "Reset commands");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkResult(vkBeginCommandBuffer(fd.command_buffer, &begin), "Begin commands");
    const VkClearValue clears[] = {{.color = {{.025f, .038f, .06f, 1}}}, {.depthStencil = {1, 0}}};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = context.render_pass;
    pass.framebuffer = fd.framebuffer;
    pass.renderArea.extent = context.swapchain_extent;
    pass.clearValueCount = 2;
    pass.pClearValues = clears;
    vkCmdBeginRenderPass(fd.command_buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);

    // Подключаем конвейер и геометрию.
    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertexBuffer.handle, &offset);
    vkCmdBindIndexBuffer(fd.command_buffer, indexBuffer.handle, 0, VK_INDEX_TYPE_UINT16);

    // Передаем матрицу и рисуем 20 треугольников одним вызовом.
    vkCmdPushConstants(fd.command_buffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(mvp), &mvp);
    vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(icosahedron::indices.size()), 1, 0, 0, 0);
    vkCmdEndRenderPass(fd.command_buffer);
    checkResult(vkEndCommandBuffer(fd.command_buffer), "End commands");
}
} // namespace application
