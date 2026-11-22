// renderer_vulkan.cpp — ver renderer_vulkan.h
#ifdef __ANDROID__
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include "renderer_vulkan.h"
#include "flower_scene.h"
#include "tga.h"

#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef __ANDROID__
#include <android/log.h>
#include <vulkan/vulkan_android.h>  // VkAndroidSurfaceCreateInfoKHR
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "FlowerVk", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "FlowerVk", __VA_ARGS__)
#else
#define LOGI(...) do { std::printf("[FlowerVk] "); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#define LOGE(...) do { std::printf("[FlowerVk][ERR] "); std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#include <fstream>
#endif

namespace {
constexpr uint32_t kMaxVerts = 1024;
constexpr VkFormat kOffscreenFormat = VK_FORMAT_R8G8B8A8_UNORM;

// Matriz orto 480x272 -> NDC. En Vulkan NDC y=-1 es ARRIBA (Y hacia abajo),
// así que PSP y=0 (arriba) debe mapear a NDC y=-1: y_ndc = 2y/272 - 1.
void ortho480x272(float m[16]) {
    std::memset(m, 0, sizeof(float) * 16);
    m[0] = 2.0f / 480.0f;
    m[5] = 2.0f / 272.0f;
    m[10] = 1.0f;
    m[12] = -1.0f;
    m[13] = -1.0f;
    m[15] = 1.0f;
}

bool checkVk(VkResult r, const char* what) {
    if (r != VK_SUCCESS) { LOGE("%s failed: %d", what, (int)r); return false; }
    return true;
}
}  // namespace

std::vector<uint8_t> Renderer::loadAsset(AAssetManager* assets, const char* path) {
#ifdef __ANDROID__
    if (!assets) return {};
    AAsset* a = AAssetManager_open(assets, path, AASSET_MODE_BUFFER);
    if (!a) { LOGE("asset no encontrado: %s", path); return {}; }
    const size_t len = (size_t)AAsset_getLength(a);
    std::vector<uint8_t> data(len);
    std::memcpy(data.data(), AAsset_getBuffer(a), len);
    AAsset_close(a);
    return data;
#else
    (void)assets;
    // Harness Linux: busca en FLOWER_ASSET_ROOT o en rutas relativas al repo.
    const char* roots[] = {
        std::getenv("FLOWER_ASSET_ROOT"),
        "app/src/main/assets",
        "../app/src/main/assets",
        "../../app/src/main/assets",
    };
    for (const char* root : roots) {
        if (!root) continue;
        std::string full = std::string(root) + "/" + path;
        std::ifstream f(full, std::ios::binary);
        if (!f) continue;
        return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                                     std::istreambuf_iterator<char>());
    }
    LOGE("asset no encontrado: %s", path);
    return {};
#endif
}

uint32_t Renderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    return UINT32_MAX;
}

bool Renderer::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "PSP Flower (Moppi, 2005) — Vulkan port";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "FlowerVk";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    std::vector<const char*> exts;
#ifdef __ANDROID__
    exts.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    exts.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#endif
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &appInfo;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    // Capas de validación solo si existen (debug); en release no se exigen.
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> avail(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, avail.data());
    for (auto& l : avail)
        if (std::strcmp(l.layerName, layers[0]) == 0) {
            ci.enabledLayerCount = 1;
            ci.ppEnabledLayerNames = layers;
            LOGI("validation layers activadas");
        }
    return checkVk(vkCreateInstance(&ci, nullptr, &m_instance), "vkCreateInstance");
}

bool Renderer::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0) { LOGE("sin dispositivos Vulkan"); return false; }
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devs.data());
    for (VkPhysicalDevice d : devs) {
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, qs.data());
        for (uint32_t i = 0; i < qCount; ++i) {
            bool graphics = (qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
#ifdef __ANDROID__
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, m_surface, &present);
            if (graphics && present) { m_physicalDevice = d; m_queueFamily = i; return true; }
#else
            if (graphics) { m_physicalDevice = d; m_queueFamily = i; return true; }
#endif
        }
    }
    LOGE("sin cola gráfica (+presentación en Android)");
    return false;
}

bool Renderer::createDevice() {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = m_queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    std::vector<const char*> exts;
#ifdef __ANDROID__
    exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#endif
    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qci;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    if (!checkVk(vkCreateDevice(m_physicalDevice, &ci, nullptr, &m_device), "vkCreateDevice"))
        return false;
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    LOGI("GPU: %s (Vulkan %u.%u.%u)", props.deviceName,
         VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion),
         VK_VERSION_PATCH(props.apiVersion));
    return true;
}

bool Renderer::createRenderPass(VkFormat format) {
    VkAttachmentDescription att{};
    att.format = format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = m_headless ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                 : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref{};
    ref.attachment = 0;
    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &att;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;
    return checkVk(vkCreateRenderPass(m_device, &ci, nullptr, &m_renderPass),
                   "vkCreateRenderPass");
}

bool Renderer::createPipeline() {
#ifdef __ANDROID__
    auto vertSpv = loadAsset(m_assets, "shaders/textured.vert.spv");
    auto fragSpv = loadAsset(m_assets, "shaders/textured.frag.spv");
#else
    auto vertSpv = loadAsset(nullptr, "shaders/textured.vert.spv");
    auto fragSpv = loadAsset(nullptr, "shaders/textured.frag.spv");
#endif
    if (vertSpv.empty() || fragSpv.empty()) { LOGE("SPIR-V no encontrado"); return false; }
    auto makeModule = [&](const std::vector<uint8_t>& spv, VkShaderModule& mod) {
        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = spv.size();
        ci.pCode = reinterpret_cast<const uint32_t*>(spv.data());
        return checkVk(vkCreateShaderModule(m_device, &ci, nullptr, &mod),
                       "vkCreateShaderModule");
    };
    VkShaderModule vertMod = VK_NULL_HANDLE, fragMod = VK_NULL_HANDLE;
    if (!makeModule(vertSpv, vertMod) || !makeModule(fragSpv, fragMod)) return false;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertMod;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragMod;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bind{};
    bind.binding = 0;
    bind.stride = sizeof(flower::Vertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[2]{};
    attrs[0].location = 0; attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = 0;
    attrs[1].location = 1; attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32_SFLOAT; attrs[1].offset = 8;
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;  // dinámicos

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Blend original PSP: ADD, SRC_ALPHA, ONE_MINUS_SRC_ALPHA.
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1; cb.pAttachments = &blend;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

    VkDescriptorSetLayoutBinding samplerBind{};
    samplerBind.binding = 0;
    samplerBind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBind.descriptorCount = 1;
    samplerBind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    // El layout solo se crea una vez: las texturas/descriptores ya
    // asignados dependen de él (al recrear el pipeline por resize se reusa).
    if (!m_descSetLayout) {
        VkDescriptorSetLayoutCreateInfo dsl{};
        dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsl.bindingCount = 1; dsl.pBindings = &samplerBind;
        if (!checkVk(vkCreateDescriptorSetLayout(m_device, &dsl, nullptr, &m_descSetLayout),
                     "vkCreateDescriptorSetLayout")) return false;
    }

    if (m_pipelineLayout) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset = 0;
    push.size = sizeof(PushConstants);
    VkPipelineLayoutCreateInfo pl{};
    pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pl.setLayoutCount = 1; pl.pSetLayouts = &m_descSetLayout;
    pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &push;
    if (!checkVk(vkCreatePipelineLayout(m_device, &pl, nullptr, &m_pipelineLayout),
                 "vkCreatePipelineLayout")) return false;

    VkGraphicsPipelineCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2; pci.pStages = stages;
    pci.pVertexInputState = &vi;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &dyn;
    pci.layout = m_pipelineLayout;
    pci.renderPass = m_renderPass;
    pci.subpass = 0;
    VkResult r = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pci, nullptr, &m_pipeline);
    vkDestroyShaderModule(m_device, vertMod, nullptr);
    vkDestroyShaderModule(m_device, fragMod, nullptr);
    return checkVk(r, "vkCreateGraphicsPipelines");
}

bool Renderer::createTextureFromTga(const uint8_t* tgaData, size_t tgaSize, Texture& tex) {
    TgaImage img;
    if (!loadTga(tgaData, tgaSize, img)) return false;
    const VkDeviceSize imgSize = (VkDeviceSize)img.width * img.height * 4;

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = imgSize;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!checkVk(vkCreateBuffer(m_device, &bci, nullptr, &staging), "staging buffer")) return false;
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(m_device, staging, &mr);
    uint32_t memType = findMemoryType(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memType == UINT32_MAX) return false;
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = memType;
    if (!checkVk(vkAllocateMemory(m_device, &mai, nullptr, &stagingMem), "staging alloc")) return false;
    vkBindBufferMemory(m_device, staging, stagingMem, 0);
    void* mapped = nullptr;
    vkMapMemory(m_device, stagingMem, 0, imgSize, 0, &mapped);
    std::memcpy(mapped, img.rgba.data(), (size_t)imgSize);
    vkUnmapMemory(m_device, stagingMem);

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {(uint32_t)img.width, (uint32_t)img.height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!checkVk(vkCreateImage(m_device, &ici, nullptr, &tex.image), "vkCreateImage")) return false;
    vkGetImageMemoryRequirements(m_device, tex.image, &mr);
    memType = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memType == UINT32_MAX) return false;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = memType;
    if (!checkVk(vkAllocateMemory(m_device, &mai, nullptr, &tex.memory), "texture alloc")) return false;
    vkBindImageMemory(m_device, tex.image, tex.memory, 0);

    // Subida con un command buffer de un solo uso.
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = m_cmdPool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device, &cai, &cmd);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkImageMemoryBarrier bar{};
    bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.image = tex.image;
    bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {(uint32_t)img.width, (uint32_t)img.height, 1};
    vkCmdCopyBufferToImage(cmd, staging, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_queue);
    vkFreeCommandBuffers(m_device, m_cmdPool, 1, &cmd);
    vkDestroyBuffer(m_device, staging, nullptr);
    vkFreeMemory(m_device, stagingMem, nullptr);

    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = tex.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!checkVk(vkCreateImageView(m_device, &vci, nullptr, &tex.view), "texture view")) return false;
    tex.width = img.width;
    tex.height = img.height;
    return true;
}

bool Renderer::createTextures(AAssetManager* assets) {
    // Sampler linear/linear (TexFilter linear/linear del original).
    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxAnisotropy = 1.0f;
    if (!checkVk(vkCreateSampler(m_device, &sci, nullptr, &m_sampler), "vkCreateSampler"))
        return false;

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 5;
    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = 5;
    pci.poolSizeCount = 1; pci.pPoolSizes = &poolSize;
    if (!checkVk(vkCreateDescriptorPool(m_device, &pci, nullptr, &m_descPool),
                 "vkCreateDescriptorPool")) return false;

    for (int i = 0; i < 5; ++i) {
        auto data = loadAsset(assets, flower::texAssetPath((flower::TexId)i));
        if (data.empty()) { LOGE("falta textura %d", i); return false; }
        if (!createTextureFromTga(data.data(), data.size(), m_textures[i])) {
            LOGE("no se pudo subir la textura %d", i);
            return false;
        }
        VkDescriptorSetAllocateInfo dai{};
        dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dai.descriptorPool = m_descPool;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &m_descSetLayout;
        if (!checkVk(vkAllocateDescriptorSets(m_device, &dai, &m_textures[i].descriptorSet),
                     "vkAllocateDescriptorSets")) return false;
        VkDescriptorImageInfo imgInfo{};
        imgInfo.sampler = m_sampler;
        imgInfo.imageView = m_textures[i].view;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = m_textures[i].descriptorSet;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &imgInfo;
        vkUpdateDescriptorSets(m_device, 1, &w, 0, nullptr);
        LOGI("textura %d: %dx%d", i, m_textures[i].width, m_textures[i].height);
    }
    return true;
}

bool Renderer::createVertexBuffer() {
    const VkDeviceSize size = kMaxVerts * sizeof(flower::Vertex);
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!checkVk(vkCreateBuffer(m_device, &bci, nullptr, &m_vertexBuffer), "vertex buffer"))
        return false;
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(m_device, m_vertexBuffer, &mr);
    uint32_t memType = findMemoryType(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memType == UINT32_MAX) return false;
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = memType;
    if (!checkVk(vkAllocateMemory(m_device, &mai, nullptr, &m_vertexBufferMemory),
                 "vertex buffer alloc")) return false;
    vkBindBufferMemory(m_device, m_vertexBuffer, m_vertexBufferMemory, 0);
    return true;
}

bool Renderer::createCommandPool() {
    VkCommandPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.queueFamilyIndex = m_queueFamily;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (!checkVk(vkCreateCommandPool(m_device, &ci, nullptr, &m_cmdPool), "cmd pool"))
        return false;
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = m_cmdPool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    return checkVk(vkAllocateCommandBuffers(m_device, &cai, &m_cmdBuffer), "cmd buffer");
}

void Renderer::recordDrawCommands(VkCommandBuffer cmd, float phase, float touchX, float touchY) {
    flower::Frame frame = flower::buildFrame(phase, touchX, touchY);
    // Vuelca todos los draws en el vertex buffer, en el orden original.
    std::vector<flower::Vertex> all;
    all.reserve(kMaxVerts);
    uint32_t firstVert[5] = {0};
    uint32_t vertCount[5] = {0};
    for (int i = 0; i < 5; ++i) {
        firstVert[i] = (uint32_t)all.size();
        vertCount[i] = (uint32_t)frame.draws[i].verts.size();
        all.insert(all.end(), frame.draws[i].verts.begin(), frame.draws[i].verts.end());
    }
    void* mapped = nullptr;
    vkMapMemory(m_device, m_vertexBufferMemory, 0,
                (VkDeviceSize)all.size() * sizeof(flower::Vertex), 0, &mapped);
    std::memcpy(mapped, all.data(), all.size() * sizeof(flower::Vertex));
    vkUnmapMemory(m_device, m_vertexBufferMemory);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertexBuffer, &offset);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    PushConstants pc{};
    ortho480x272(pc.ortho);
    for (int i = 0; i < 5; ++i) {
        // La textura se elige por TexId del draw, NO por índice de draw
        // (bg es el draw 0 pero su TexId es 3, etc.).
        pc.tint[0] = 1.0f; pc.tint[1] = 1.0f; pc.tint[2] = 1.0f;
        pc.tint[3] = frame.draws[i].alpha;
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, sizeof(pc), &pc);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                0, 1, &m_textures[frame.draws[i].tex].descriptorSet, 0, nullptr);
        vkCmdDraw(cmd, vertCount[i], 1, firstVert[i], 0);
    }
}

#ifdef __ANDROID__
// ---------------- Android: swapchain ----------------
bool Renderer::init(ANativeWindow* window, AAssetManager* assets) {
    m_assets = assets;
    if (!createInstance()) return false;
    VkAndroidSurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    sci.window = window;
    if (!checkVk(vkCreateAndroidSurfaceKHR(m_instance, &sci, nullptr, &m_surface),
                 "vkCreateAndroidSurfaceKHR")) return false;
    if (!pickPhysicalDevice() || !createDevice()) return false;
    if (!createRenderPass(m_swapFormat)) return false;
    if (!createCommandPool()) return false;
    // Las texturas y el pipeline necesitan el pool de comandos y el layout.
    // createPipeline carga los SPIR-V con el AAssetManager guardado.
    if (!createPipeline()) return false;
    if (!createTextures(assets)) return false;
    if (!createVertexBuffer()) return false;
    onWindowResized(window);
    VkSemaphoreCreateInfo semi{};
    semi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(m_device, &semi, nullptr, &m_imageAvailableSem);
    vkCreateSemaphore(m_device, &semi, nullptr, &m_renderFinishedSem);
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(m_device, &fci, nullptr, &m_fence);
    m_initialized = true;
    return true;
}

void Renderer::onWindowResized(ANativeWindow* window) {
    vkDeviceWaitIdle(m_device);
    for (auto fb : m_swapFramebuffers) vkDestroyFramebuffer(m_device, fb, nullptr);
    for (auto v : m_swapImageViews) vkDestroyImageView(m_device, v, nullptr);
    if (m_swapchain) vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    m_swapFramebuffers.clear(); m_swapImageViews.clear(); m_swapImages.clear();

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);
    m_swapWidth = caps.currentExtent.width;
    m_swapHeight = caps.currentExtent.height;
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fmtCount, fmts.data());
    m_swapFormat = fmts[0].format;
    // Preferir B8G8R8A8_SRGB si está disponible.
    for (auto& f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB) m_swapFormat = f.format;
    if (m_renderPass) { vkDestroyRenderPass(m_device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }
    createRenderPass(m_swapFormat);
    // El pipeline depende del render pass: recrearlo.
    if (m_pipeline) { vkDestroyPipeline(m_device, m_pipeline, nullptr); m_pipeline = VK_NULL_HANDLE; }
    createPipeline();

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = m_surface;
    sci.minImageCount = 2;
    sci.imageFormat = m_swapFormat;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent = {m_swapWidth, m_swapHeight};
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    if (!checkVk(vkCreateSwapchainKHR(m_device, &sci, nullptr, &m_swapchain),
                 "vkCreateSwapchainKHR")) return;
    uint32_t imgCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imgCount, nullptr);
    m_swapImages.resize(imgCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imgCount, m_swapImages.data());
    for (VkImage img : m_swapImages) {
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = img;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = m_swapFormat;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        vkCreateImageView(m_device, &vci, nullptr, &view);
        m_swapImageViews.push_back(view);
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = m_renderPass;
        fci.attachmentCount = 1; fci.pAttachments = &view;
        fci.width = m_swapWidth; fci.height = m_swapHeight; fci.layers = 1;
        VkFramebuffer fb = VK_NULL_HANDLE;
        vkCreateFramebuffer(m_device, &fci, nullptr, &fb);
        m_swapFramebuffers.push_back(fb);
    }
}

bool Renderer::drawFrame(float phase, float touchX, float touchY) {
    if (!m_initialized || m_swapchain == VK_NULL_HANDLE) return false;
    vkWaitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_device, 1, &m_fence);
    uint32_t imgIndex = 0;
    VkResult r = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                       m_imageAvailableSem, VK_NULL_HANDLE, &imgIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) return false;  // main.cpp recrea
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return false;
    vkResetCommandBuffer(m_cmdBuffer, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(m_cmdBuffer, &bi);
    VkClearValue clear{};
    clear.color.float32[0] = 0.0f; clear.color.float32[1] = 0.0f;
    clear.color.float32[2] = 0.0f; clear.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = m_renderPass;
    rp.framebuffer = m_swapFramebuffers[imgIndex];
    rp.renderArea = {{0, 0}, {m_swapWidth, m_swapHeight}};
    rp.clearValueCount = 1; rp.pClearValues = &clear;
    vkCmdBeginRenderPass(m_cmdBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{};
    vp.width = (float)m_swapWidth; vp.height = (float)m_swapHeight;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(m_cmdBuffer, 0, 1, &vp);
    VkRect2D sc{};
    sc.extent = {m_swapWidth, m_swapHeight};
    vkCmdSetScissor(m_cmdBuffer, 0, 1, &sc);
    recordDrawCommands(m_cmdBuffer, phase, touchX, touchY);
    vkCmdEndRenderPass(m_cmdBuffer);
    vkEndCommandBuffer(m_cmdBuffer);
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1; si.pWaitSemaphores = &m_imageAvailableSem;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1; si.pCommandBuffers = &m_cmdBuffer;
    si.signalSemaphoreCount = 1; si.pSignalSemaphores = &m_renderFinishedSem;
    vkQueueSubmit(m_queue, 1, &si, m_fence);
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &m_renderFinishedSem;
    pi.swapchainCount = 1; pi.pSwapchains = &m_swapchain;
    pi.pImageIndices = &imgIndex;
    r = vkQueuePresentKHR(m_queue, &pi);
    return r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR;
}

#else
// ---------------- Linux headless (harness) ----------------
bool Renderer::initHeadless() {
    m_headless = true;
    if (!createInstance()) return false;
    if (!pickPhysicalDevice() || !createDevice()) return false;
    if (!createRenderPass(kOffscreenFormat)) return false;
    if (!createCommandPool()) return false;
    if (!createPipeline()) return false;
    if (!createTextures(nullptr)) return false;
    if (!createVertexBuffer()) return false;
    // Target offscreen 480x272.
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = kOffscreenFormat;
    ici.extent = {480, 272, 1};
    ici.mipLevels = 1; ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!checkVk(vkCreateImage(m_device, &ici, nullptr, &m_offscreenImage), "offscreen image"))
        return false;
    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(m_device, m_offscreenImage, &mr);
    uint32_t memType = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memType == UINT32_MAX) return false;
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size; mai.memoryTypeIndex = memType;
    if (!checkVk(vkAllocateMemory(m_device, &mai, nullptr, &m_offscreenMemory), "offscreen alloc"))
        return false;
    vkBindImageMemory(m_device, m_offscreenImage, m_offscreenMemory, 0);
    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = m_offscreenImage;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = kOffscreenFormat;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (!checkVk(vkCreateImageView(m_device, &vci, nullptr, &m_offscreenView), "offscreen view"))
        return false;
    VkFramebufferCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = m_renderPass;
    fci.attachmentCount = 1; fci.pAttachments = &m_offscreenView;
    fci.width = 480; fci.height = 272; fci.layers = 1;
    if (!checkVk(vkCreateFramebuffer(m_device, &fci, nullptr, &m_framebuffer), "offscreen fb"))
        return false;
    m_initialized = true;
    return true;
}

bool Renderer::drawFrame(float phase, float touchX, float touchY) {
    if (!m_initialized) return false;
    vkResetCommandBuffer(m_cmdBuffer, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (!checkVk(vkBeginCommandBuffer(m_cmdBuffer, &bi), "begin cmd")) return false;
    VkClearValue clear{};
    clear.color.float32[0] = 0.0f; clear.color.float32[1] = 0.0f;
    clear.color.float32[2] = 0.0f; clear.color.float32[3] = 1.0f;
    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = m_renderPass;
    rp.framebuffer = m_framebuffer;
    rp.renderArea = {{0, 0}, {480, 272}};
    rp.clearValueCount = 1; rp.pClearValues = &clear;
    vkCmdBeginRenderPass(m_cmdBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{};
    vp.width = 480.0f; vp.height = 272.0f; vp.maxDepth = 1.0f;
    vkCmdSetViewport(m_cmdBuffer, 0, 1, &vp);
    VkRect2D sc{};
    sc.extent = {480, 272};
    vkCmdSetScissor(m_cmdBuffer, 0, 1, &sc);
    recordDrawCommands(m_cmdBuffer, phase, touchX, touchY);
    vkCmdEndRenderPass(m_cmdBuffer);
    if (!checkVk(vkEndCommandBuffer(m_cmdBuffer), "end cmd")) return false;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &m_cmdBuffer;
    if (!checkVk(vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE), "queue submit")) return false;
    vkQueueWaitIdle(m_queue);
    return true;
}

bool Renderer::readback(std::vector<uint8_t>& rgba, int& w, int& h) {
    if (!m_initialized || m_device == VK_NULL_HANDLE) return false;
    w = 480; h = 272;
    const VkDeviceSize size = (VkDeviceSize)w * h * 4;
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (!checkVk(vkCreateBuffer(m_device, &bci, nullptr, &buf), "readback buffer")) return false;
    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(m_device, buf, &mr);
    uint32_t memType = findMemoryType(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (memType == UINT32_MAX) return false;
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size; mai.memoryTypeIndex = memType;
    if (!checkVk(vkAllocateMemory(m_device, &mai, nullptr, &mem), "readback alloc")) return false;
    vkBindBufferMemory(m_device, buf, mem, 0);
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.commandPool = m_cmdPool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_device, &cai, &cmd);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {480, 272, 1};
    vkCmdCopyImageToBuffer(cmd, m_offscreenImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           buf, 1, &region);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1; si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_queue);
    vkFreeCommandBuffers(m_device, m_cmdPool, 1, &cmd);
    void* mapped = nullptr;
    vkMapMemory(m_device, mem, 0, size, 0, &mapped);
    rgba.assign((uint8_t*)mapped, (uint8_t*)mapped + size);
    vkUnmapMemory(m_device, mem);
    vkDestroyBuffer(m_device, buf, nullptr);
    vkFreeMemory(m_device, mem, nullptr);
    return true;
}
#endif

void Renderer::cleanup() {
    if (m_device == VK_NULL_HANDLE) {
        if (m_instance) vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        return;
    }
    vkDeviceWaitIdle(m_device);
    for (auto fb : m_swapFramebuffers) vkDestroyFramebuffer(m_device, fb, nullptr);
    for (auto v : m_swapImageViews) vkDestroyImageView(m_device, v, nullptr);
    if (m_framebuffer) vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
    if (m_offscreenView) vkDestroyImageView(m_device, m_offscreenView, nullptr);
    if (m_offscreenImage) vkDestroyImage(m_device, m_offscreenImage, nullptr);
    if (m_offscreenMemory) vkFreeMemory(m_device, m_offscreenMemory, nullptr);
    if (m_vertexBuffer) vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
    if (m_vertexBufferMemory) vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);
    for (int i = 0; i < 5; ++i) {
        if (m_textures[i].view) vkDestroyImageView(m_device, m_textures[i].view, nullptr);
        if (m_textures[i].image) vkDestroyImage(m_device, m_textures[i].image, nullptr);
        if (m_textures[i].memory) vkFreeMemory(m_device, m_textures[i].memory, nullptr);
    }
    if (m_sampler) vkDestroySampler(m_device, m_sampler, nullptr);
    if (m_descPool) vkDestroyDescriptorPool(m_device, m_descPool, nullptr);
    if (m_descSetLayout) vkDestroyDescriptorSetLayout(m_device, m_descSetLayout, nullptr);
    if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_renderPass) vkDestroyRenderPass(m_device, m_renderPass, nullptr);
    if (m_cmdPool) vkDestroyCommandPool(m_device, m_cmdPool, nullptr);
    if (m_imageAvailableSem) vkDestroySemaphore(m_device, m_imageAvailableSem, nullptr);
    if (m_renderFinishedSem) vkDestroySemaphore(m_device, m_renderFinishedSem, nullptr);
    if (m_fence) vkDestroyFence(m_device, m_fence, nullptr);
    if (m_swapchain) vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyDevice(m_device, nullptr);
    vkDestroyInstance(m_instance, nullptr);
    m_device = VK_NULL_HANDLE;
    m_instance = VK_NULL_HANDLE;
    m_initialized = false;
}
