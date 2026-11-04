#include "application.hpp"
#include "icosahedron.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <imgui.h>
#include <lodepng.h>
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
Buffer vertexBuffer, uniformBuffer, stagingBuffer;
VkDeviceSize uniformStride{}; // шаг между записями динамического UBO
VkImage textureImage{};
VmaAllocation textureAllocation{};
VkImageView textureView{};
VkSampler textureSampler{};
VkCommandPool uploadPool{}; // временный пул команд загрузки
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
    glm::vec4 specular;   // w — степень блика
    glm::vec4 ambient;    // w — интенсивность
};
static_assert(sizeof(SceneData) == 144);

glm::vec3 cameraPosition(0.0f, 0.0f, 4.0f);
glm::vec3 cameraRotation(0.0f); // pitch, yaw, roll в градусах
float cameraFov = 45.0f;
float lightAzimuth = 30.0f, lightElevation = 40.0f;
glm::vec4 lightColor(1.0f, 1.0f, 1.0f, 1.0f);
glm::vec4 specular(0.7f, 0.7f, 0.7f, 32.0f);
glm::vec4 ambient(1.0f, 1.0f, 1.0f, 0.15f);

// проверка результата Vulkan
void checkResult(VkResult result, const char *action) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(action) + ": " + std::to_string(result));
}

// hostVisible: CPU пишет в UBO/staging; геометрию заполняет GPU
Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible) {
    auto &context = graphics::internal::context;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    if (hostVisible) {
        allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT;
    } else {
        allocation.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }
    VmaAllocationInfo mapped{};
    Buffer buffer;
    checkResult(vmaCreateBuffer(context.allocator, &info, &allocation, &buffer.handle, &buffer.allocation,
                                &mapped),
                "Create buffer");
    buffer.mapped = mapped.pMappedData;
    return buffer;
}

// загрузка шейдера из SPIR-V
VkShaderModule loadShader(const char *filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error(std::string("Cannot open shader: ") + filename);
    const auto length = file.tellg();
    if (length <= 0 || length % sizeof(uint32_t) != 0)
        throw std::runtime_error("Invalid SPIR-V length");
    std::vector<uint32_t> code(static_cast<size_t>(length) / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(code.data()), length);
    if (!file)
        throw std::runtime_error("Cannot read SPIR-V");
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = code.size() * sizeof(uint32_t);
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

// один раз загружаем текстуру и вершины через staging в память устройства
void uploadResources() {
    const auto &context = graphics::internal::context;
    std::vector<unsigned char> pixels;
    unsigned width{}, height{};
    const unsigned error = lodepng::decode(pixels, width, height, "textures/albedo.png");
    if (error)
        throw std::runtime_error(std::string("Cannot load textures/albedo.png: ") +
                                 lodepng_error_text(error));

    // изображение будет целью копирования и источником цвета для шейдера
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_R8G8B8A8_SRGB;
    image.extent = {width, height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    allocation.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    checkResult(vmaCreateImage(context.allocator, &image, &allocation, &textureImage,
                               &textureAllocation, nullptr), "Create texture");

    const auto vertices = icosahedron::makeVertices();
    const VkDeviceSize vertexSize = sizeof(vertices), imageSize = pixels.size();
    vertexBuffer = createBuffer(vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    // staging хранит сначала вершины, затем плотно упакованные пиксели RGBA8
    stagingBuffer = createBuffer(vertexSize + imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    auto *data = static_cast<unsigned char *>(stagingBuffer.mapped);
    std::memcpy(data, vertices.data(), vertexSize);
    std::memcpy(data + vertexSize, pixels.data(), imageSize);
    checkResult(vmaFlushAllocation(context.allocator, stagingBuffer.allocation, 0, VK_WHOLE_SIZE),
                "Flush staging buffer");

    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool.queueFamilyIndex = context.graphics_queue_index;
    checkResult(vkCreateCommandPool(context.device, &pool, nullptr, &uploadPool), "Create upload pool");
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = uploadPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer commands{};
    checkResult(vkAllocateCommandBuffers(context.device, &commandInfo, &commands), "Allocate upload commands");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkResult(vkBeginCommandBuffer(commands, &begin), "Begin upload");

    // UNDEFINED → TRANSFER_DST: подготавливаем изображение к записи
    VkImageMemoryBarrier imageBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.image = textureImage;
    imageBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &imageBarrier);

    const VkBufferCopy vertexCopy{0, 0, vertexSize};
    vkCmdCopyBuffer(commands, stagingBuffer.handle, vertexBuffer.handle, 1, &vertexCopy);
    VkBufferImageCopy imageCopy{};
    imageCopy.bufferOffset = vertexSize;
    imageCopy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    imageCopy.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(commands, stagingBuffer.handle, textureImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopy);

    // запись при копировании должна завершиться до чтения при отрисовке
    VkBufferMemoryBarrier vertexBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    vertexBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vertexBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vertexBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vertexBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    vertexBarrier.buffer = vertexBuffer.handle;
    vertexBarrier.size = VK_WHOLE_SIZE;
    imageBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    imageBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 1, &vertexBarrier, 1, &imageBarrier);
    checkResult(vkEndCommandBuffer(commands), "End upload");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commands;
    checkResult(vkQueueSubmit(context.graphics_queue, 1, &submit, VK_NULL_HANDLE), "Submit upload");
    checkResult(vkQueueWaitIdle(context.graphics_queue), "Wait for upload");
    // GPU закончил копирование: временные ресурсы больше не нужны
    vkDestroyCommandPool(context.device, uploadPool, nullptr);
    uploadPool = VK_NULL_HANDLE;
    destroyBuffer(stagingBuffer);

    // view выбирает изображение, sampler задаёт правила выборки его цвета
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = textureImage;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = image.format;
    view.subresourceRange = imageBarrier.subresourceRange;
    checkResult(vkCreateImageView(context.device, &view, nullptr, &textureView), "Create texture view");
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    checkResult(vkCreateSampler(context.device, &sampler, nullptr, &textureSampler), "Create sampler");
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
    // входы шейдера: 0 — позиция, 1 — нормаль, 2 — UV
    VkVertexInputBindingDescription binding{0, sizeof(icosahedron::Vertex),
                                            VK_VERTEX_INPUT_RATE_VERTEX};
    const VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(icosahedron::Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(icosahedron::Vertex, normal)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(icosahedron::Vertex, uv)},
    };
    VkPipelineVertexInputStateCreateInfo input{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 3;
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

// вращение вокруг объекта и приближение мышью
void updateCameraFromMouse() {
    const auto &io = ImGui::GetIO();
    const bool dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    if (io.WantCaptureMouse || (!dragging && io.MouseWheel == 0.0f))
        return;

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

// матрицы камеры и параметры освещения для UBO
SceneData makeSceneData() {
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
    return SceneData{projection * view, glm::vec4(cameraPosition, 1),
                     glm::vec4(lightDirection, 0), lightColor, specular, ambient};
}

} // namespace

// создание ресурсов сцены
bool initialize() {
    try {
        uploadResources();
        const auto &context = graphics::internal::context;
        const auto device = context.device;
        // шаг записей UBO округляется вверх по требованию устройства
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(context.physical_device, &properties);
        const VkDeviceSize alignment = properties.limits.minUniformBufferOffsetAlignment;
        uniformStride = (sizeof(SceneData) + alignment - 1) / alignment * alignment;
        // одна фигура — одна запись; для N фигур размер был бы N * uniformStride
        uniformBuffer = createBuffer(uniformStride, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        const VkDescriptorSetLayoutBinding bindings[] = {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        };
        VkDescriptorSetLayoutCreateInfo setLayout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        setLayout.bindingCount = 2;
        setLayout.pBindings = bindings;
        checkResult(vkCreateDescriptorSetLayout(device, &setLayout, nullptr, &descriptorLayout),
                    "Create descriptor layout");
        const VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
        };
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1;
        pool.poolSizeCount = 2;
        pool.pPoolSizes = poolSizes;
        checkResult(vkCreateDescriptorPool(device, &pool, nullptr, &descriptorPool),
                    "Create descriptor pool");
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool = descriptorPool;
        allocation.descriptorSetCount = 1;
        allocation.pSetLayouts = &descriptorLayout;
        checkResult(vkAllocateDescriptorSets(device, &allocation, &descriptorSet), "Allocate descriptor set");
        // в одном наборе: запись UBO и пара изображение–сэмплер
        const VkDescriptorBufferInfo bufferInfo{uniformBuffer.handle, 0, sizeof(SceneData)};
        const VkDescriptorImageInfo imageInfo{textureSampler, textureView,
                                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet writes[2]{};
        for (auto &write : writes) {
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSet;
            write.descriptorCount = 1;
        }
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        writes[0].pBufferInfo = &bufferInfo;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);

        // подключаем дескрипторы к схеме конвейера
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &descriptorLayout;
        checkResult(vkCreatePipelineLayout(device, &layout, nullptr,
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
    if (textureSampler)
        vkDestroySampler(device, textureSampler, nullptr);
    if (textureView)
        vkDestroyImageView(device, textureView, nullptr);
    if (textureImage)
        vmaDestroyImage(graphics::internal::context.allocator, textureImage, textureAllocation);
    textureSampler = VK_NULL_HANDLE;
    textureView = VK_NULL_HANDLE;
    textureImage = VK_NULL_HANDLE;
    textureAllocation = VK_NULL_HANDLE;
    // также освобождаем временные ресурсы, если загрузка прервалась ошибкой
    if (uploadPool)
        vkDestroyCommandPool(device, uploadPool, nullptr);
    uploadPool = VK_NULL_HANDLE;
    destroyBuffer(stagingBuffer);
    destroyBuffer(uniformBuffer);
    destroyBuffer(vertexBuffer);
    pipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
}

// настройки камеры, света и материала
void update([[maybe_unused]] double time) {
    updateCameraFromMouse();

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 610), ImGuiCond_FirstUseEver);
    ImGui::Begin("Lab 3");
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
    ImGui::TextUnformatted("Albedo: textures/albedo.png");
    ImGui::ColorEdit3("Specular color", &specular.x);
    ImGui::SliderFloat("Shininess", &specular.w, 1.0f, 128.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SeparatorText("Ambient light");
    ImGui::ColorEdit3("Ambient color", &ambient.x);
    ImGui::SliderFloat("Ambient intensity", &ambient.w, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::PopItemWidth();
    ImGui::End();
}

// обновление UBO и запись команд кадра
void render(const graphics::internal::FrameData &frame) {
    const auto &context = graphics::internal::context;

    const SceneData scene = makeSceneData();
    // после prepare() буфер можно перезаписать
    std::memcpy(uniformBuffer.mapped, &scene, sizeof(scene));
    checkResult(vmaFlushAllocation(context.allocator, uniformBuffer.allocation, 0, sizeof(scene)),
                "Flush scene buffer");

    checkResult(vkResetCommandBuffer(frame.command_buffer, 0), "Reset commands");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkResult(vkBeginCommandBuffer(frame.command_buffer, &begin), "Begin commands");
    const VkClearValue clears[] = {{.color = {{.025f, .038f, .06f, 1}}}, {.depthStencil = {1, 0}}};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = context.render_pass;
    pass.framebuffer = frame.framebuffer;
    pass.renderArea.extent = context.swapchain_extent;
    pass.clearValueCount = 2;
    pass.pClearValues = clears;
    vkCmdBeginRenderPass(frame.command_buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(frame.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(frame.command_buffer, 0, 1, &vertexBuffer.handle, &offset);
    // запись i находится по смещению i * uniformStride; у нас одна запись, i = 0
    const uint32_t dynamicOffset = 0;
    vkCmdBindDescriptorSets(frame.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            0, 1, &descriptorSet, 1, &dynamicOffset);

    // 20 граней, по 3 вершины с общей нормалью
    vkCmdDraw(frame.command_buffer, static_cast<uint32_t>(icosahedron::indices.size()), 1, 0, 0);
    vkCmdEndRenderPass(frame.command_buffer);
    checkResult(vkEndCommandBuffer(frame.command_buffer), "End commands");
}
} // namespace application
