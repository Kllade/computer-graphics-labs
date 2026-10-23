#include "application.hpp"
#include "icosahedron.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <imgui.h>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace application {
namespace {

struct Buffer {
    VkBuffer handle{};
    VmaAllocation allocation{};
    void *mapped{};
};
Buffer vertexBuffer, uniformBuffer;
VkDescriptorSetLayout descriptorLayout{};
VkDescriptorPool descriptorPool{};
VkDescriptorSet descriptorSet{};
VkPipelineLayout pipelineLayout{};
VkPipeline pipeline{};
VkShaderModule vertexShader{}, fragmentShader{};

// поля UBO совпадают с блоком Scene в шейдерах
struct SceneData {
    glm::mat4 viewProjection; // матрица P * V
    glm::vec4 cameraPosition;
    glm::vec4 lightDirection; // направление к свету
    glm::vec4 lightColor; // w — интенсивность
    glm::vec4 albedo;
    glm::vec4 specular;   // w — степень блика
    glm::vec4 ambient;    // w — интенсивность
};
static_assert(sizeof(SceneData) == 160);

glm::vec3 cameraPosition(0.0f, 0.0f, 4.0f);
glm::vec3 cameraRotation(0.0f); // pitch, yaw, roll в градусах
float cameraFov = 45.0f;
float lightAzimuth = 30.0f, lightElevation = 40.0f;
glm::vec4 lightColor(1.0f, 1.0f, 1.0f, 1.0f);
glm::vec4 albedo(0.25f, 0.65f, 0.9f, 1.0f);
glm::vec4 specular(0.7f, 0.7f, 0.7f, 32.0f);
glm::vec4 ambient(1.0f, 1.0f, 1.0f, 0.15f);

// проверка результата Vulkan
void checkResult(VkResult result, const char *action) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(action) + ": " + std::to_string(result));
}

// создание и заполнение буфера
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
    out.mapped = mapped.pMappedData;
    std::memcpy(out.mapped, data, static_cast<size_t>(size));
    // запись становится видимой GPU
    VkResult result = vmaFlushAllocation(context.allocator, out.allocation, 0, size);
    if (result != VK_SUCCESS) {
        vmaDestroyBuffer(context.allocator, out.handle, out.allocation);
        checkResult(result, "Flush buffer");
    }
    return out;
}

// загрузка шейдера из SPIR-V
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

// освобождение буфера и памяти
void destroyBuffer(Buffer &buffer) {
    if (buffer.handle)
        vmaDestroyBuffer(graphics::internal::context.allocator, buffer.handle, buffer.allocation);
    buffer = {};
}

// настройка графического конвейера
void createGraphicsPipeline() {
    auto &context = graphics::internal::context;
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
    // входы шейдера: 0 — позиция, 1 — нормаль
    VkVertexInputBindingDescription binding{0, sizeof(icosahedron::Vertex),
                                            VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(icosahedron::Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(icosahedron::Vertex, normal)},
    };
    VkPipelineVertexInputStateCreateInfo input{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 2;
    input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
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
    // ближние поверхности закрывают дальние
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

// создание ресурсов сцены
bool initialize() {
    try {
        // создаём вершины с нормалями
        const auto vertices = icosahedron::makeVertices();
        // записываем вершины в буфер
        vertexBuffer = createBuffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    vertices.data());

        // создаём UBO для параметров сцены
        const SceneData initial{};
        uniformBuffer = createBuffer(sizeof(SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &initial);
        const auto device = graphics::internal::context.device;
        // создаём дескрипторы для доступа шейдеров к UBO
        const VkDescriptorSetLayoutBinding binding{
            0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo setLayout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setLayout.bindingCount = 1;
        setLayout.pBindings = &binding;
        checkResult(vkCreateDescriptorSetLayout(device, &setLayout, nullptr, &descriptorLayout),
                    "Create descriptor layout");
        const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &poolSize;
        checkResult(vkCreateDescriptorPool(device, &pool, nullptr, &descriptorPool),
                    "Create descriptor pool");
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool = descriptorPool;
        allocation.descriptorSetCount = 1;
        allocation.pSetLayouts = &descriptorLayout;
        checkResult(vkAllocateDescriptorSets(device, &allocation, &descriptorSet), "Allocate descriptor set");
        // связываем дескриптор с UBO
        const VkDescriptorBufferInfo bufferInfo{uniformBuffer.handle, 0, sizeof(SceneData)};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

        // подключаем дескрипторы к схеме конвейера
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &descriptorLayout;
        checkResult(vkCreatePipelineLayout(graphics::internal::context.device, &layout, nullptr,
                                          &pipelineLayout), "Create pipeline layout");
        // создаём графический конвейер
        createGraphicsPipeline();
        return true;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        shutdown();
        return false;
    }
}

// ожидание GPU и освобождение сцены
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
    if (descriptorPool)
        vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    if (descriptorLayout)
        vkDestroyDescriptorSetLayout(device, descriptorLayout, nullptr);
    descriptorPool = VK_NULL_HANDLE;
    descriptorLayout = VK_NULL_HANDLE;
    descriptorSet = VK_NULL_HANDLE;
    destroyBuffer(uniformBuffer);
    destroyBuffer(vertexBuffer);
    pipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
}

// настройки камеры, света и материала
void update([[maybe_unused]] double time) {
    const auto &io = ImGui::GetIO();
    // мышь вращает камеру вокруг центра объекта
    const bool dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    if (!io.WantCaptureMouse && (dragging || io.MouseWheel != 0.0f)) {
        const float distance = glm::length(cameraPosition);
        float azimuth = distance > 0.0001f ? std::atan2(cameraPosition.x, cameraPosition.z) : 0.0f;
        float elevation = distance > 0.0001f
            ? std::asin(glm::clamp(cameraPosition.y / distance, -1.0f, 1.0f)) : 0.0f;
        if (dragging) {
            azimuth -= glm::radians(io.MouseDelta.x * 0.2f);
            elevation += glm::radians(io.MouseDelta.y * 0.2f);
        }
        elevation = glm::clamp(elevation, glm::radians(-89.0f), glm::radians(89.0f));
        // колёсико меняет расстояние до объекта
        const float radius = glm::clamp(distance * std::exp(-io.MouseWheel * 0.1f), 1.2f, 50.0f);
        cameraPosition = radius * glm::vec3(std::sin(azimuth) * std::cos(elevation),
                                            std::sin(elevation),
                                            std::cos(azimuth) * std::cos(elevation));
        // камера всегда смотрит на центр икосаэдра
        cameraRotation = glm::vec3(-glm::degrees(elevation),
                                   std::remainder(glm::degrees(azimuth), 360.0f), 0.0f);
    }

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 610), ImGuiCond_FirstUseEver);
    ImGui::Begin("Lab 2");
    ImGui::PushItemWidth(200.0f);
    ImGui::TextUnformatted("Icosahedron / Variant 11");
    ImGui::SeparatorText("Camera");
    ImGui::DragFloat3("Position", &cameraPosition.x, 0.05f);
    ImGui::SliderFloat3("Pitch / Yaw / Roll", &cameraRotation.x, -180.0f, 180.0f);
    ImGui::SliderFloat("FOV", &cameraFov, 10.0f, 120.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::Button("Reset camera")) {
        cameraPosition = glm::vec3(0, 0, 4);
        cameraRotation = glm::vec3(0);
        cameraFov = 45.0f;
    }
    ImGui::SeparatorText("Directional light (towards source)");
    ImGui::SliderFloat("Azimuth", &lightAzimuth, -180.0f, 180.0f);
    ImGui::SliderFloat("Elevation", &lightElevation, -90.0f, 90.0f);
    ImGui::ColorEdit3("Light color", &lightColor.x);
    ImGui::SliderFloat("Light intensity", &lightColor.w, 0.0f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SeparatorText("Material");
    ImGui::ColorEdit3("Albedo", &albedo.x);
    ImGui::ColorEdit3("Specular color", &specular.x);
    ImGui::SliderFloat("Shininess", &specular.w, 1.0f, 128.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SeparatorText("Ambient light");
    ImGui::ColorEdit3("Ambient color", &ambient.x);
    ImGui::SliderFloat("Ambient intensity", &ambient.w, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopItemWidth();
    ImGui::End();
}

// обновление UBO и запись команд кадра
void render(const graphics::internal::FrameData &fd) {
    const auto &context = graphics::internal::context;

    const float aspect = static_cast<float>(context.swapchain_extent.width) /
                         context.swapchain_extent.height;
    // вращение камеры: Ry * Rx * Rz
    glm::mat4 rotation = glm::rotate(glm::mat4(1), glm::radians(cameraRotation.y), glm::vec3(0, 1, 0));
    rotation = glm::rotate(rotation, glm::radians(cameraRotation.x), glm::vec3(1, 0, 0));
    rotation = glm::rotate(rotation, glm::radians(cameraRotation.z), glm::vec3(0, 0, 1));
    // view — обратное преобразование камеры
    const glm::mat4 view = glm::transpose(rotation) * glm::translate(glm::mat4(1), -cameraPosition);
    // перспектива: near = 0.1, far = 100
    glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(cameraFov), aspect, 0.1f, 100.0f);
    projection[1][1] *= -1; // коррекция Y для Vulkan
    // направление к свету из двух углов
    const float azimuth = glm::radians(lightAzimuth), elevation = glm::radians(lightElevation);
    const glm::vec3 lightDirection(std::cos(elevation) * std::sin(azimuth), std::sin(elevation),
                                    std::cos(elevation) * std::cos(azimuth));
    // матрица модели единичная
    const SceneData scene{projection * view, glm::vec4(cameraPosition, 1),
                          glm::vec4(lightDirection, 0), lightColor, albedo, specular, ambient};
    // после prepare() буфер можно перезаписать
    std::memcpy(uniformBuffer.mapped, &scene, sizeof(scene));
    checkResult(vmaFlushAllocation(context.allocator, uniformBuffer.allocation, 0, sizeof(scene)),
                "Flush scene buffer");

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

    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertexBuffer.handle, &offset);
    vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            0, 1, &descriptorSet, 0, nullptr);

    // 20 граней, по 3 вершины с общей нормалью
    vkCmdDraw(fd.command_buffer, static_cast<uint32_t>(icosahedron::indices.size()), 1, 0, 0);
    vkCmdEndRenderPass(fd.command_buffer);
    checkResult(vkEndCommandBuffer(fd.command_buffer), "End commands");
}
} // namespace application
