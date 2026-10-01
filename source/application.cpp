#include "application.hpp"
#include "scene.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <imgui.h>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace application {
namespace {
struct Buffer {
    VkBuffer handle{};
    VmaAllocation allocation{};
    void *mapped{};
};
struct alignas(16) ObjectData {
    scene::Mat4 mvp, model;
    float tint[4];
    float options[4];
};
static_assert(sizeof(ObjectData) == 160);
// Геометрия общая. Матрицы, цвет, UBO и набор дескрипторов — свои у каждого объекта.
constexpr uint32_t objectCount = 3;
struct Object {
    scene::ObjectState state;
    Buffer uniformBuffer;
    VkDescriptorSet descriptorSet{};
};
std::array<Object, objectCount> objects;
scene::Camera camera;
int selectedObject = 0;
Buffer vertexBuffer, indexBuffer;
VkDescriptorSetLayout descriptorLayout{};
VkDescriptorPool descriptorPool{};
VkPipelineLayout pipelineLayout{};
VkPipeline pipeline{};
double previousTime = -1;
float panelWidth = 350;
int smokeFrame = 0;

void check(VkResult result, const char *action) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(action) + ": " + std::to_string(result));
}
Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, const void *data = nullptr) {
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
    check(vmaCreateBuffer(context.allocator, &info, &allocation, &out.handle, &out.allocation,
                          &mapped),
          "Create buffer");
    out.mapped = mapped.pMappedData;
    if (data) {
        std::memcpy(out.mapped, data, static_cast<size_t>(size));
        VkResult result = vmaFlushAllocation(context.allocator, out.allocation, 0, size);
        if (result != VK_SUCCESS) {
            vmaDestroyBuffer(context.allocator, out.handle, out.allocation);
            check(result, "Flush buffer");
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
    check(vkCreateShaderModule(graphics::internal::context.device, &info, nullptr, &result),
          "Create shader");
    return result;
}
void createPipeline() {
    auto &c = graphics::internal::context;
    VkShaderModule vert{}, frag{};
    try {
        vert = loadShader("shaders/icosahedron.vert.spv");
        frag = loadShader("shaders/icosahedron.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (auto &stage : stages) {
            stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.pName = "main";
        }
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        VkVertexInputBindingDescription binding{0, sizeof(scene::Vertex),
                                                VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attributes[] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(scene::Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(scene::Vertex, color)}};
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
        info.renderPass = c.render_pass;
        check(vkCreateGraphicsPipelines(c.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
              "Create graphics pipeline");
    } catch (...) {
        if (vert)
            vkDestroyShaderModule(c.device, vert, nullptr);
        if (frag)
            vkDestroyShaderModule(c.device, frag, nullptr);
        throw;
    }
    vkDestroyShaderModule(c.device, vert, nullptr);
    vkDestroyShaderModule(c.device, frag, nullptr);
}
void resetScene() {
    camera = scene::Camera{};
    for (uint32_t i = 0; i < objectCount; ++i) {
        auto &state = objects[i].state;
        state = scene::ObjectState{};
        state.position = {2.2f * (static_cast<float>(i) - 1), 0, 0};
        state.scale = {0.8f, 0.8f, 0.8f};
        state.radius = 0.4f;
        state.height = 0.3f;
    }
    // Три разных оттенка помогают увидеть независимость наборов дескрипторов.
    objects[0].state.tint[1] = 0.45f;
    objects[0].state.tint[2] = 0.45f;
    objects[1].state.tint[0] = 0.45f;
    objects[1].state.tint[2] = 0.45f;
    objects[2].state.tint[0] = 0.45f;
    objects[2].state.tint[1] = 0.6f;
}

void ui() {
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
    auto &controls = objects[selectedObject].state;
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
        ImGui::SliderFloat3("Rotation", &controls.angles.x, -180, 180, "%.0f");
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
} // namespace
bool initialize() {
    try {
        auto &c = graphics::internal::context;
        auto vertices = scene::vertices();
        vertexBuffer =
            createBuffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices.data());
        indexBuffer = createBuffer(sizeof(scene::indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                   scene::indices.data());
        resetScene();
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = 1;
        layout.pBindings = &binding;
        check(vkCreateDescriptorSetLayout(c.device, &layout, nullptr, &descriptorLayout),
              "Descriptor layout");
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, objectCount};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = objectCount;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &size;
        check(vkCreateDescriptorPool(c.device, &pool, nullptr, &descriptorPool), "Descriptor pool");
        // Каждый набор ссылается на отдельный UBO; схема binding 0 у всех одинаковая.
        for (auto &object : objects) {
            object.uniformBuffer =
                createBuffer(sizeof(ObjectData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            allocation.descriptorPool = descriptorPool;
            allocation.descriptorSetCount = 1;
            allocation.pSetLayouts = &descriptorLayout;
            check(vkAllocateDescriptorSets(c.device, &allocation, &object.descriptorSet),
                  "Descriptor set");

            VkDescriptorBufferInfo buffer{object.uniformBuffer.handle, 0, sizeof(ObjectData)};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = object.descriptorSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &buffer;
            vkUpdateDescriptorSets(c.device, 1, &write, 0, nullptr);
        }
        VkPipelineLayoutCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineInfo.setLayoutCount = 1;
        pipelineInfo.pSetLayouts = &descriptorLayout;
        check(vkCreatePipelineLayout(c.device, &pipelineInfo, nullptr, &pipelineLayout),
              "Pipeline layout");
        createPipeline();
        ImGui::StyleColorsDark();
        auto &style = ImGui::GetStyle();
        style.WindowPadding = {18, 16};
        style.FramePadding = {6, 4};
        style.ItemSpacing = {8, 8};
        style.FrameRounding = 4;
        style.GrabRounding = 4;
        style.Colors[ImGuiCol_WindowBg] = {.055f, .075f, .10f, 1};
        style.Colors[ImGuiCol_CheckMark] = {.25f, .85f, .75f, 1};
        style.Colors[ImGuiCol_SliderGrab] = {.25f, .85f, .75f, 1};
        std::cout << "Lab 1: 3 icosahedra, 3 uniform buffers, 3 descriptor sets. Ready.\n";
        return true;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        shutdown();
        return false;
    }
}
void shutdown() {
    auto &c = graphics::internal::context;
    vkDeviceWaitIdle(c.device);
    if (pipeline)
        vkDestroyPipeline(c.device, pipeline, nullptr);
    if (pipelineLayout)
        vkDestroyPipelineLayout(c.device, pipelineLayout, nullptr);
    if (descriptorPool)
        vkDestroyDescriptorPool(c.device, descriptorPool, nullptr);
    if (descriptorLayout)
        vkDestroyDescriptorSetLayout(c.device, descriptorLayout, nullptr);
    for (auto &object : objects) {
        if (object.uniformBuffer.handle)
            vmaDestroyBuffer(c.allocator, object.uniformBuffer.handle,
                             object.uniformBuffer.allocation);
        object.uniformBuffer = {};
        object.descriptorSet = VK_NULL_HANDLE;
    }
    for (Buffer *b : {&indexBuffer, &vertexBuffer}) {
        if (b->handle)
            vmaDestroyBuffer(c.allocator, b->handle, b->allocation);
        *b = {};
    }
    pipeline = VK_NULL_HANDLE;
    pipelineLayout = VK_NULL_HANDLE;
    descriptorPool = VK_NULL_HANDLE;
    descriptorLayout = VK_NULL_HANDLE;
}
void update(double time) {
    double dt = previousTime < 0 ? 0 : time - previousTime;
    previousTime = time;
    ui();
    // Анимация каждого объекта обновляется независимо от выбора в интерфейсе.
    for (auto &object : objects)
        scene::advance(object.state, dt);

    if (std::getenv("CG_LAB_SMOKE")) {
        int stage = smokeFrame++ / 30;
        camera.perspectiveProjection = stage % 2 == 0;
        for (uint32_t i = 0; i < objectCount; ++i) {
            auto &state = objects[i].state;
            state.vertexColors = (stage + i) % 2 == 0;
            state.playing = stage >= 2 && i != 1;
            state.speed = 0.5f + i;
            state.scale = {0.7f, 0.5f + 0.2f * i, 0.9f};
        }
        if (smokeFrame % 30 == 1)
            std::cout << "Smoke stage " << stage
                      << ": 3 objects, independent colors/scale/animation, projection="
                      << camera.perspectiveProjection << '\n';
    }
}
void render(const graphics::internal::FrameData &fd) {
    const auto &c = graphics::internal::context;
    auto &io = ImGui::GetIO();
    const uint32_t left = static_cast<uint32_t>(panelWidth * c.swapchain_extent.width /
                                                std::max(io.DisplaySize.x, 1.f));
    const uint32_t width = std::max(1u, c.swapchain_extent.width - left);
    const float aspect = static_cast<float>(width) / c.swapchain_extent.height;
    const auto viewProjection = scene::projection(camera, aspect) * scene::view(camera);
    // prepare() уже дождался fence предыдущего кадра: CPU может обновить все UBO.
    for (auto &object : objects) {
        const auto &state = object.state;
        ObjectData data{};
        data.model = scene::model(state);
        data.mvp = viewProjection * data.model;
        std::copy_n(state.tint, 3, data.tint);
        data.tint[3] = 1;
        data.options[0] = state.vertexColors ? 1.f : 0.f;
        data.options[1] = state.lighting ? 1.f : 0.f;
        std::memcpy(object.uniformBuffer.mapped, &data, sizeof(data));
        check(vmaFlushAllocation(c.allocator, object.uniformBuffer.allocation, 0, sizeof(data)),
              "Flush uniform buffer");
    }
    check(vkResetCommandBuffer(fd.command_buffer, 0), "Reset commands");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(fd.command_buffer, &begin), "Begin commands");
    const VkClearValue clears[] = {{.color = {{.025f, .038f, .06f, 1}}}, {.depthStencil = {1, 0}}};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = c.render_pass;
    pass.framebuffer = fd.framebuffer;
    pass.renderArea.extent = c.swapchain_extent;
    pass.clearValueCount = 2;
    pass.pClearValues = clears;
    vkCmdBeginRenderPass(fd.command_buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{static_cast<float>(left),
                        0,
                        static_cast<float>(width),
                        static_cast<float>(c.swapchain_extent.height),
                        0,
                        1};
    VkRect2D scissor{{static_cast<int32_t>(left), 0}, {width, c.swapchain_extent.height}};
    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);
    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertexBuffer.handle, &offset);
    vkCmdBindIndexBuffer(fd.command_buffer, indexBuffer.handle, 0, VK_INDEX_TYPE_UINT16);
    // Перед каждой отрисовкой выбираем данные именно этого объекта.
    for (const auto &object : objects) {
        vkCmdBindDescriptorSets(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                0, 1, &object.descriptorSet, 0, nullptr);
        vkCmdDrawIndexed(fd.command_buffer, static_cast<uint32_t>(scene::indices.size()), 1, 0, 0,
                         0);
    }
    vkCmdEndRenderPass(fd.command_buffer);
    check(vkEndCommandBuffer(fd.command_buffer), "End commands");
}
} // namespace application
