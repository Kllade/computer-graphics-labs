#include "application.hpp"
#include "scene.hpp"
#include "icosahedron.hpp"
#include "graphics.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <imgui.h>
#include <iostream>
#include <stdexcept>

namespace application {
namespace {
// 1. Данные GPU. Порядок полей совпадает с uniform-блоком обоих шейдеров.
struct alignas(16) ObjectUniforms {
    math::Mat4 mvp; // как преобразуем вершины для вывода на экран 
    math::Mat4 model; 
    float baseColor[4];
    float useVertexColors;
    float enableShading;
    float padding[2];
};
static_assert(sizeof(ObjectUniforms) == 160);
static_assert(offsetof(ObjectUniforms, baseColor) == 128);
static_assert(offsetof(ObjectUniforms, useVertexColors) == 144);
static_assert(offsetof(ObjectUniforms, enableShading) == 148);
static_assert(offsetof(ObjectUniforms, padding) == 152);

// 2. CPU-настройки и GPU-ресурсы. Индекс i относится к одному и тому же объекту.
constexpr uint32_t objectCount = 3;
std::array<scene::ObjectState, objectCount> objects;
std::array<graphics::Buffer, objectCount> uniformBuffers;
std::array<VkDescriptorSet, objectCount> descriptorSets{};
scene::Camera camera;
int selectedObject = 0;
double previousTime = -1;
float panelWidth = 350;

// 3. Геометрия и конвейер общие для всех трёх икосаэдров.
graphics::Buffer vertexBuffer, indexBuffer;
VkDescriptorSetLayout descriptorLayout{};
VkDescriptorPool descriptorPool{};
VkPipelineLayout pipelineLayout{};
VkPipeline pipeline{};

void createGraphicsPipeline() {
    auto &context = graphics::internal::context;
    VkShaderModule vert{}, frag{};
    try {
        // 1. Вершинный и фрагментный шейдеры.
        vert = graphics::loadShader("shaders/icosahedron.vert.spv");
        frag = graphics::loadShader("shaders/icosahedron.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (auto &stage : stages) {
            stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.pName = "main";
        }
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        // 2. Как Vulkan читает позицию и цвет из Vertex.
        VkVertexInputBindingDescription binding{0, sizeof(icosahedron::Vertex),
                                                VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attributes[] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(icosahedron::Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(icosahedron::Vertex, color)}};
        VkPipelineVertexInputStateCreateInfo input{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        input.vertexBindingDescriptionCount = 1;
        input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = 2;
        input.pVertexAttributeDescriptions = attributes;
        // 3. Сборка треугольников, viewport/scissor и растеризация.
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
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
        VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = states;
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
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout;
        info.renderPass = context.render_pass;
        graphics::checkResult(vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
              "Create graphics pipeline");
    } catch (...) {
        if (vert)
            vkDestroyShaderModule(context.device, vert, nullptr);
        if (frag)
            vkDestroyShaderModule(context.device, frag, nullptr);
        throw;
    }
    vkDestroyShaderModule(context.device, vert, nullptr);
    vkDestroyShaderModule(context.device, frag, nullptr);
}
void resetScene() {
    camera = scene::Camera{};
    for (uint32_t i = 0; i < objectCount; ++i) {
        auto &state = objects[i];
        state = scene::ObjectState{};
        state.position = {2.2f * (static_cast<float>(i) - 1), 0, 0};
        state.scale = {0.8f, 0.8f, 0.8f};
        state.radius = 0.4f;
        state.height = 0.3f;
    }
    // Три разных оттенка помогают увидеть независимость наборов дескрипторов.
    objects[0].tint[1] = 0.45f;
    objects[0].tint[2] = 0.45f;
    objects[1].tint[0] = 0.45f;
    objects[1].tint[2] = 0.45f;
    objects[2].tint[0] = 0.45f;
    objects[2].tint[1] = 0.6f;
}

void drawInterface() {
    auto &io = ImGui::GetIO();
    panelWidth = std::min(350.f, io.DisplaySize.x * .42f);
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({panelWidth, io.DisplaySize.y});
    ImGui::Begin("Lab controls", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse);
    ImGui::TextColored({.35f, .88f, .81f, 1}, "COMPUTER GRAPHICS / LAB 01");
    ImGui::SetWindowFontScale(1.45f);
    ImGui::TextUnformatted("Icosahedron");
    ImGui::SetWindowFontScale(1);
    ImGui::TextDisabled("Variant 11  /  student #23");
    ImGui::TextDisabled("12 vertices   30 edges   20 faces");
    ImGui::Separator();
    ImGui::PushItemWidth(-110);
    ImGui::Combo("Object", &selectedObject,
                 "1 - Left\0"
                 "2 - Center\0"
                 "3 - Right\0");
    ImGui::TextDisabled("3 objects / 3 descriptor sets");
    auto &controls = objects[selectedObject];
    if (ImGui::CollapsingHeader("01  Projection", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::RadioButton("Perspective", camera.perspectiveProjection))
            camera.perspectiveProjection = true;
        ImGui::SameLine();
        if (ImGui::RadioButton("Ortho", !camera.perspectiveProjection))
            camera.perspectiveProjection = false;
        if (camera.perspectiveProjection)
            ImGui::SliderFloat("Field of view", &camera.fov, 20, 85, "%.0f deg");
        else
            ImGui::SliderFloat("Ortho height", &camera.orthoHeight, 1.5f, 12, "%.2f");
    }
    if (ImGui::CollapsingHeader("02  Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat3("Position", &controls.position.x, -3, 3, "%.2f");
        ImGui::SliderFloat3("Rotation", &controls.rotationDegrees.x, -180, 180, "%.0f");
        ImGui::SliderFloat3("Scale", &controls.scale.x, .1f, 3, "%.2f");
        ImGui::TextDisabled("XYZ  /  model = T * Rz * Ry * Rx * S");
    }
    if (ImGui::CollapsingHeader("03  Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button(controls.playing ? "Pause" : "Play", {100, 0}))
            controls.playing = !controls.playing;
        ImGui::SameLine();
        if (ImGui::Button("Rewind")) {
            controls.animationTime = 0;
            controls.playing = false;
        }
        ImGui::SliderFloat("Speed", &controls.speed, .1f, 3, "%.2fx");
        ImGui::SliderFloat("Radius", &controls.radius, 0, 2, "%.2f");
        ImGui::SliderFloat("Height", &controls.height, 0, 1.5f, "%.2f");
        ImGui::SliderFloat("Spin", &controls.spin, 0, 120, "%.0f deg/s");
        ImGui::TextDisabled("3D path: sin(t), sin(2t), cos(t)");
    }
    if (ImGui::CollapsingHeader("04 / 05  Color", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Tint", controls.tint, ImGuiColorEditFlags_NoInputs);
        ImGui::Checkbox("Procedural vertex colors", &controls.vertexColors);
        ImGui::Checkbox("Facet shading", &controls.lighting);
        ImGui::TextDisabled("Optional two-sided shading");
        ImGui::TextDisabled("Final color = vertex color x tint");
    }
    if (ImGui::CollapsingHeader("Camera")) {
        ImGui::SliderFloat("Distance", &camera.cameraDistance, 3, 18, "%.2f");
        ImGui::SliderFloat("Yaw", &camera.cameraYaw, -180, 180, "%.0f");
        ImGui::SliderFloat("Pitch", &camera.cameraPitch, -80, 80, "%.0f");
    }
    if (ImGui::Button("Reset scene", {-1, 0}))
        resetScene();
    ImGui::TextDisabled("Drag in scene: orbit / wheel: zoom");
    ImGui::TextDisabled("%.0f FPS | Vulkan / MoltenVK", io.Framerate);
    ImGui::PopItemWidth();
    ImGui::End();
}

void updateCameraFromMouse() {
    const auto &io = ImGui::GetIO();
    if (!io.WantCaptureMouse && io.MousePos.x > panelWidth) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            camera.cameraYaw -= io.MouseDelta.x * .35f;
            camera.cameraPitch =
                std::clamp(camera.cameraPitch + io.MouseDelta.y * .35f, -80.f, 80.f);
        }
        if (camera.perspectiveProjection)
            camera.cameraDistance =
                std::clamp(camera.cameraDistance - io.MouseWheel * .4f, 3.f, 18.f);
        else
            camera.orthoHeight = std::clamp(camera.orthoHeight - io.MouseWheel * .3f, 1.5f, 12.f);
    }
}
void createGeometryBuffers() {
    auto vertices = icosahedron::createVertices();
    vertexBuffer =
        graphics::createBuffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices.data());
    indexBuffer = graphics::createBuffer(sizeof(icosahedron::indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                               icosahedron::indices.data());
}

void createObjectDescriptors() {
    auto &context = graphics::internal::context;
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 1;
    layout.pBindings = &binding;
    graphics::checkResult(vkCreateDescriptorSetLayout(context.device, &layout, nullptr, &descriptorLayout),
                          "Descriptor layout");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, objectCount};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = objectCount;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    graphics::checkResult(vkCreateDescriptorPool(context.device, &pool, nullptr, &descriptorPool), "Descriptor pool");
    // Каждый набор ссылается на отдельный UBO; схема binding 0 у всех одинаковая.
    // Каждый объект имеет свой UBO и набор; binding 0 у всех одинаковый.
    for (uint32_t i = 0; i < objectCount; ++i) {
        uniformBuffers[i] =
            graphics::createBuffer(sizeof(ObjectUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorPool = descriptorPool;
        allocation.descriptorSetCount = 1;
        allocation.pSetLayouts = &descriptorLayout;
        graphics::checkResult(vkAllocateDescriptorSets(context.device, &allocation, &descriptorSets[i]),
                              "Descriptor set");

        VkDescriptorBufferInfo buffer{uniformBuffers[i].handle, 0, sizeof(ObjectUniforms)};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = descriptorSets[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &buffer;
        vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
    }
    VkPipelineLayoutCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineInfo.setLayoutCount = 1;
    pipelineInfo.pSetLayouts = &descriptorLayout;
    graphics::checkResult(vkCreatePipelineLayout(context.device, &pipelineInfo, nullptr, &pipelineLayout),
                          "Pipeline layout");
}

} // namespace

bool initialize() {
    try {
        // Этап 1: параметры сцены на CPU.
        resetScene();
        previousTime = -1;
        // Этап 2: одна сетка икосаэдра в vertex/index buffers.
        createGeometryBuffers();
        // Этап 3: три UBO и три набора дескрипторов, по одному на объект.
        createObjectDescriptors();
        // Этап 4: шейдеры и настройки графического конвейера.
        createGraphicsPipeline();
        ImGui::StyleColorsDark();
        std::cout << "Lab 1: 3 icosahedra, 3 uniform buffers, 3 descriptor sets. Ready.\n";
        return true;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        shutdown();
        return false;
    }
}

void shutdown() {
    auto &context = graphics::internal::context;
    vkDeviceWaitIdle(context.device);
    if (pipeline)
        vkDestroyPipeline(context.device, pipeline, nullptr);
    if (pipelineLayout)
        vkDestroyPipelineLayout(context.device, pipelineLayout, nullptr);
    if (descriptorPool)
        vkDestroyDescriptorPool(context.device, descriptorPool, nullptr);
    if (descriptorLayout)
        vkDestroyDescriptorSetLayout(context.device, descriptorLayout, nullptr);
    for (auto &buffer : uniformBuffers)
        graphics::destroyBuffer(buffer);
    descriptorSets.fill(VK_NULL_HANDLE);
    graphics::destroyBuffer(indexBuffer);
    graphics::destroyBuffer(vertexBuffer);
    pipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    descriptorPool = VK_NULL_HANDLE;
    descriptorLayout = VK_NULL_HANDLE;
}

void update(double time) {
    double dt = previousTime < 0 ? 0 : time - previousTime;
    previousTime = time;
    drawInterface();
    updateCameraFromMouse();
    // Анимация каждого объекта обновляется независимо от выбора в интерфейсе.
    for (auto &object : objects)
        scene::updateAnimation(object, dt);
}

void render(const graphics::internal::FrameData &fd) {
    const auto &context = graphics::internal::context;
    auto &io = ImGui::GetIO();
    const uint32_t left = static_cast<uint32_t>(panelWidth * context.swapchain_extent.width /
                                                std::max(io.DisplaySize.x, 1.f));
    const uint32_t width = std::max(1u, context.swapchain_extent.width - left);
    const float aspect = static_cast<float>(width) / context.swapchain_extent.height;
    // Этап 1: P * V * M и параметры цвета для каждого объекта.
    const auto viewProjection = scene::projectionMatrix(camera, aspect) * scene::viewMatrix(camera);
    // prepare() уже дождался fence предыдущего кадра: CPU может обновить все UBO.
    for (uint32_t i = 0; i < objectCount; ++i) {
        const auto &object = objects[i];
        ObjectUniforms data{};
        data.model = scene::modelMatrix(object);
        data.mvp = viewProjection * data.model;
        std::copy_n(object.tint, 3, data.baseColor);
        data.baseColor[3] = 1;
        data.useVertexColors = object.vertexColors ? 1.f : 0.f;
        data.enableShading = object.lighting ? 1.f : 0.f;
        std::memcpy(uniformBuffers[i].mapped, &data, sizeof(data));
        graphics::checkResult(vmaFlushAllocation(context.allocator, uniformBuffers[i].allocation, 0, sizeof(data)),
                             "Flush uniform buffer");
    }
    // Этап 2: начать command buffer и render pass, очистить цвет и глубину.
    graphics::checkResult(vkResetCommandBuffer(fd.command_buffer, 0), "Reset commands");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    graphics::checkResult(vkBeginCommandBuffer(fd.command_buffer, &begin), "Begin commands");
    const VkClearValue clears[] = {{.color = {{.025f, .038f, .06f, 1}}}, {.depthStencil = {1, 0}}};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = context.render_pass;
    pass.framebuffer = fd.framebuffer;
    pass.renderArea.extent = context.swapchain_extent;
    pass.clearValueCount = 2;
    pass.pClearValues = clears;
    vkCmdBeginRenderPass(fd.command_buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
    // Этап 3: область сцены справа от панели ImGui.
    VkViewport viewport{static_cast<float>(left),
                        0,
                        static_cast<float>(width),
                        static_cast<float>(context.swapchain_extent.height),
                        0,
                        1};
    VkRect2D scissor{{static_cast<int32_t>(left), 0}, {width, context.swapchain_extent.height}};
    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);
    // Этап 4: общие pipeline и геометрия.
    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertexBuffer.handle, &offset);
    vkCmdBindIndexBuffer(fd.command_buffer, indexBuffer.handle, 0, VK_INDEX_TYPE_UINT16);
    // Этап 5: набор данных отдельного объекта -> его отрисовка.
    for (uint32_t i = 0; i < objectCount; ++i) {
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                0, 1, &descriptorSets[i], 0, nullptr);
        vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(icosahedron::indices.size()), 1, 0, 0,
                         0);
    }
    // Этап 6: закончить запись. Отправку и показ делает шаблон преподавателя.
    vkCmdEndRenderPass(fd.command_buffer);
    graphics::checkResult(vkEndCommandBuffer(fd.command_buffer), "End commands");
}
} // namespace application
