// The hardware layer on Vulkan (Linux, Android; Windows where asked for): Vulkan 1.1 with
// VK_KHR_swapchain and nothing else required, so that it runs on every Vulkan Android device.
// Functions are loaded through the window system's vkGetInstanceProcAddr (no link to the
// loader); memory comes from AMD's Vulkan Memory Allocator.
//
// Textures remember their layout, and passes say which textures they read, so the barriers
// between uploading, drawing into and sampling a texture are put in here. Frames in flight: two,
// each with its own command buffer, descriptor pool and memory for what the CPU writes once.

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

#include "device.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace kompoz::gpu {
namespace {

// The functions used, loaded once per instance and device.
#define KOMPOZ_VK_INSTANCE_FUNCTIONS(X)                                                                                                    \
    X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceQueueFamilyProperties)        \
    X(vkGetPhysicalDeviceSurfaceSupportKHR) X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR)           \
    X(vkGetPhysicalDeviceSurfacePresentModesKHR) X(vkDestroySurfaceKHR) X(vkCreateDevice) X(vkGetDeviceProcAddr)                           \
    X(vkEnumerateDeviceExtensionProperties) X(vkCreateDebugUtilsMessengerEXT) X(vkDestroyDebugUtilsMessengerEXT)                          \
    X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceFormatProperties)

#define KOMPOZ_VK_DEVICE_FUNCTIONS(X)                                                                                                      \
    X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR)                            \
    X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR) X(vkQueuePresentKHR) X(vkQueueSubmit) X(vkCreateCommandPool)                      \
    X(vkDestroyCommandPool) X(vkResetCommandPool) X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer)                \
    X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences) X(vkCreateSemaphore) X(vkDestroySemaphore)                      \
    X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateRenderPass) X(vkDestroyRenderPass) X(vkCreateFramebuffer)                         \
    X(vkDestroyFramebuffer) X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout)          \
    X(vkCreateGraphicsPipelines) X(vkDestroyPipeline) X(vkCreateDescriptorSetLayout) X(vkDestroyDescriptorSetLayout)                       \
    X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkResetDescriptorPool) X(vkAllocateDescriptorSets)                              \
    X(vkUpdateDescriptorSets) X(vkCreateSampler) X(vkDestroySampler) X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass)                         \
    X(vkCmdBindPipeline) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdPushConstants) X(vkCmdBindDescriptorSets)                           \
    X(vkCmdBindVertexBuffers) X(vkCmdDraw) X(vkCmdPipelineBarrier) X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer)                    \
    X(vkCreatePipelineCache) X(vkDestroyPipelineCache)

struct Api {
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    PFN_vkCreateInstance vkCreateInstance = nullptr;
    PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = nullptr;
#define KOMPOZ_VK_MEMBER(name) PFN_##name name = nullptr;
    KOMPOZ_VK_INSTANCE_FUNCTIONS(KOMPOZ_VK_MEMBER)
    KOMPOZ_VK_DEVICE_FUNCTIONS(KOMPOZ_VK_MEMBER)
#undef KOMPOZ_VK_MEMBER
};

constexpr int kFramesInFlight = 2;
constexpr VkDeviceSize kChunkSize = 4 << 20;  // per-frame memory comes in chunks of this (or one as big as asked)

VkFormat vkFormat(Format f) { return f == Format::BGRA8 ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8G8B8A8_UNORM; }

VkBlendFactor vkFactor(BlendFactor f) {
    switch (f) {
    case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    }
    return VK_BLEND_FACTOR_ONE;
}

VkBlendOp vkOp(BlendOp op) {
    switch (op) {
    case BlendOp::Add: return VK_BLEND_OP_ADD;
    case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOp::Min: return VK_BLEND_OP_MIN;
    case BlendOp::Max: return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

VkFormat vkVertexFormat(VertexFormat f) {
    switch (f) {
    case VertexFormat::Float: return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::UInt4: return VK_FORMAT_R32G32B32A32_UINT;
    }
    return VK_FORMAT_R32G32B32A32_SFLOAT;
}

VkSamplerAddressMode vkWrap(Wrap w) {
    switch (w) {
    case Wrap::Clamp: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case Wrap::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case Wrap::Mirror: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case Wrap::Transparent: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

class VulkanDevice;

class VulkanTexture final : public Texture {
public:
    VulkanTexture(VulkanDevice& device, int width, int height, Format format, VkImage image, VkImageView view, VmaAllocation allocation)
        : Texture(width, height, format), device_(device), image(image), view(view), allocation(allocation) {}
    ~VulkanTexture() override;

    VulkanDevice& device_;
    VkImage image;
    VkImageView view;
    VmaAllocation allocation;  // null: a swapchain image (the swapchain owns it)
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::vector<std::pair<VkRenderPass, VkFramebuffer>> framebuffers;
};

class VulkanPipeline final : public Pipeline {
public:
    VulkanPipeline(VulkanDevice& device, VkPipeline pipeline) : device_(device), pipeline(pipeline) {}
    ~VulkanPipeline() override;
    VulkanDevice& device_;
    VkPipeline pipeline;
};

class VulkanDevice final : public Device {
public:
    Api vk;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    std::function<void(int*, int*)> drawableSize;
    std::atomic<int> errors{0};  // validation errors reported

    // Swapchain.
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapFormat = VK_FORMAT_B8G8R8A8_UNORM;
    Format swapFormatKompoz = Format::BGRA8;
    VkExtent2D swapExtent{};
    std::vector<std::unique_ptr<VulkanTexture>> swapImages;
    std::vector<VkSemaphore> renderFinished;  // per swapchain image
    uint32_t swapIndex = 0;
    bool acquired = false;

    // Per frame in flight.
    struct Chunk {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        uint8_t* mapped = nullptr;
        VkDeviceSize size = 0, used = 0;
    };
    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> buffers;  // allocated so far (readPixels splits a frame)
        size_t nextBuffer = 0;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkDescriptorPool descriptors = VK_NULL_HANDLE;
        std::vector<Chunk> chunks;
        std::vector<std::function<void()>> deletions;  // run when the frame's GPU work is done
    };
    std::array<Frame, kFramesInFlight> frames;
    int frameIndex = 0;
    bool inFrame = false;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    bool waitImage = false;  // the next submit waits for the swapchain image to be acquired

    // Render state.
    std::map<std::pair<VkFormat, bool>, VkRenderPass> renderPasses;  // by format and clear
    std::vector<std::pair<Sampler, VkSampler>> samplers;
    VulkanTexture* target = nullptr;
    VkPipeline boundPipeline = VK_NULL_HANDLE;
    std::array<std::pair<VulkanTexture*, Sampler>, kMaxTextures> textures{};
    bool texturesDirty = true;

    ~VulkanDevice() override {
        if (device) {
            vk.vkDeviceWaitIdle(device);
            for (Frame& f : frames) runDeletions(f);
            destroySwapchain();
            for (Frame& f : frames) {
                for (Chunk& c : f.chunks) vmaDestroyBuffer(allocator, c.buffer, c.allocation);
                vk.vkDestroyDescriptorPool(device, f.descriptors, nullptr);
                vk.vkDestroyCommandPool(device, f.pool, nullptr);
                vk.vkDestroyFence(device, f.fence, nullptr);
                vk.vkDestroySemaphore(device, f.imageAvailable, nullptr);
            }
            for (auto& [key, pass] : renderPasses) vk.vkDestroyRenderPass(device, pass, nullptr);
            for (auto& [desc, sampler] : samplers) vk.vkDestroySampler(device, sampler, nullptr);
            vk.vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            vk.vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
            vk.vkDestroyPipelineCache(device, pipelineCache, nullptr);
            if (allocator) vmaDestroyAllocator(allocator);
            vk.vkDestroyDevice(device, nullptr);
        }
        if (surface) vk.vkDestroySurfaceKHR(instance, surface, nullptr);
        if (messenger) vk.vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
        if (instance) vk.vkDestroyInstance(instance, nullptr);
    }

    // Everything but the swapchain, made once; false (and *error) if this system can't.
    bool init(const Surface& s, std::string* error) {
        auto fail = [&](std::string why) {
            if (error) *error = std::move(why);
            return false;
        };
        if (!s.vulkanGetInstanceProcAddr || !s.createVulkanSurface) return fail("no Vulkan surface from the window system");
        drawableSize = s.drawableSize;
        vk.vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(s.vulkanGetInstanceProcAddr);
        auto global = [&](const char* name) { return vk.vkGetInstanceProcAddr(VK_NULL_HANDLE, name); };
        vk.vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(global("vkCreateInstance"));
        vk.vkEnumerateInstanceLayerProperties = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(global("vkEnumerateInstanceLayerProperties"));
        vk.vkEnumerateInstanceExtensionProperties =
            reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(global("vkEnumerateInstanceExtensionProperties"));
        if (!vk.vkCreateInstance) return fail("no Vulkan loader");

        // Validation: asked for, and the layer is there.
        bool validation = false;
        if (const char* v = std::getenv("KOMPOZ_GPU_VALIDATION"); v && std::string_view(v) == "1") {
            uint32_t count = 0;
            vk.vkEnumerateInstanceLayerProperties(&count, nullptr);
            std::vector<VkLayerProperties> layers(count);
            vk.vkEnumerateInstanceLayerProperties(&count, layers.data());
            validation = std::any_of(layers.begin(), layers.end(),
                                     [](const VkLayerProperties& l) { return std::string_view(l.layerName) == "VK_LAYER_KHRONOS_validation"; });
            if (!validation) log("kompoz: KOMPOZ_GPU_VALIDATION=1 but there is no VK_LAYER_KHRONOS_validation");
        }
        std::vector<const char*> extensions = s.vulkanInstanceExtensions;
        if (validation) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        const char* layer = "VK_LAYER_KHRONOS_validation";
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "Kompoz";
        app.pEngineName = "Kompoz";
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ici.pApplicationInfo = &app;
        ici.enabledExtensionCount = uint32_t(extensions.size());
        ici.ppEnabledExtensionNames = extensions.data();
        ici.enabledLayerCount = validation ? 1 : 0;
        ici.ppEnabledLayerNames = &layer;
        if (VkResult r = vk.vkCreateInstance(&ici, nullptr, &instance); r != VK_SUCCESS)
            return fail("vkCreateInstance failed (" + std::to_string(int(r)) + ")");
#define KOMPOZ_VK_LOAD(name) vk.name = reinterpret_cast<PFN_##name>(vk.vkGetInstanceProcAddr(instance, #name));
        KOMPOZ_VK_INSTANCE_FUNCTIONS(KOMPOZ_VK_LOAD)
#undef KOMPOZ_VK_LOAD
        if (validation && vk.vkCreateDebugUtilsMessengerEXT) {
            VkDebugUtilsMessengerCreateInfoEXT mci{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mci.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                     const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) -> VkBool32 {
                log(std::string("kompoz: vulkan: ") + data->pMessage);
                if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++static_cast<VulkanDevice*>(user)->errors;
                return VK_FALSE;
            };
            mci.pUserData = this;
            vk.vkCreateDebugUtilsMessengerEXT(instance, &mci, nullptr, &messenger);
        }
        uint64_t rawSurface = 0;
        if (!s.createVulkanSurface(instance, &rawSurface)) return fail("no Vulkan surface for the window");
        surface = VkSurfaceKHR(rawSurface);

        if (!pickPhysicalDevice()) return fail("no GPU that can present to the window with Vulkan 1.1");
        float priority = 1;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = queueFamily;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        const char* swapchainExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = 1;
        dci.ppEnabledExtensionNames = &swapchainExt;
        if (VkResult r = vk.vkCreateDevice(physical, &dci, nullptr, &device); r != VK_SUCCESS)
            return fail("vkCreateDevice failed (" + std::to_string(int(r)) + ")");
#define KOMPOZ_VK_LOAD(name) vk.name = reinterpret_cast<PFN_##name>(vk.vkGetDeviceProcAddr(device, #name));
        KOMPOZ_VK_DEVICE_FUNCTIONS(KOMPOZ_VK_LOAD)
#undef KOMPOZ_VK_LOAD
        vk.vkGetDeviceQueue(device, queueFamily, 0, &queue);

        VmaVulkanFunctions functions{};
        functions.vkGetInstanceProcAddr = vk.vkGetInstanceProcAddr;
        functions.vkGetDeviceProcAddr = vk.vkGetDeviceProcAddr;
        VmaAllocatorCreateInfo aci{};
        aci.vulkanApiVersion = VK_API_VERSION_1_1;
        aci.physicalDevice = physical;
        aci.device = device;
        aci.instance = instance;
        aci.pVulkanFunctions = &functions;
        if (vmaCreateAllocator(&aci, &allocator) != VK_SUCCESS) return fail("vmaCreateAllocator failed");

        VkPipelineCacheCreateInfo pcci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        vk.vkCreatePipelineCache(device, &pcci, nullptr, &pipelineCache);

        // One binding layout for every pipeline: textures at bindings 1..kMaxTextures, uniforms as
        // push constants.
        std::array<VkDescriptorSetLayoutBinding, kMaxTextures> bindings{};
        for (int i = 0; i < kMaxTextures; ++i)
            bindings[size_t(i)] = {uint32_t(i + 1), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo slci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        slci.bindingCount = uint32_t(bindings.size());
        slci.pBindings = bindings.data();
        vk.vkCreateDescriptorSetLayout(device, &slci, nullptr, &setLayout);
        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, uint32_t(kMaxUniforms)};
        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &setLayout;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &push;
        vk.vkCreatePipelineLayout(device, &plci, nullptr, &pipelineLayout);

        for (Frame& f : frames) {
            VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            cpci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            cpci.queueFamilyIndex = queueFamily;
            vk.vkCreateCommandPool(device, &cpci, nullptr, &f.pool);
            VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            vk.vkCreateFence(device, &fci, nullptr, &f.fence);
            VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            vk.vkCreateSemaphore(device, &sci, nullptr, &f.imageAvailable);
            VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096 * kMaxTextures};
            VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            dpci.maxSets = 4096;
            dpci.poolSizeCount = 1;
            dpci.pPoolSizes = &size;
            vk.vkCreateDescriptorPool(device, &dpci, nullptr, &f.descriptors);
        }
        log("kompoz: GPU: Vulkan on " + deviceName());
        return true;
    }

    // A GPU with a queue that draws and presents to the surface: the one KOMPOZ_GPU_DEVICE
    // names part of, else an integrated one (it saves power), else a discrete one, else any.
    bool pickPhysicalDevice() {
        uint32_t count = 0;
        vk.vkEnumeratePhysicalDevices(instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count);
        vk.vkEnumeratePhysicalDevices(instance, &count, devices.data());
        const char* wanted = std::getenv("KOMPOZ_GPU_DEVICE");
        int bestRank = -1;
        for (VkPhysicalDevice d : devices) {
            VkPhysicalDeviceProperties p;
            vk.vkGetPhysicalDeviceProperties(d, &p);
            if (p.apiVersion < VK_API_VERSION_1_1) continue;
            uint32_t extCount = 0;
            vk.vkEnumerateDeviceExtensionProperties(d, nullptr, &extCount, nullptr);
            std::vector<VkExtensionProperties> exts(extCount);
            vk.vkEnumerateDeviceExtensionProperties(d, nullptr, &extCount, exts.data());
            if (std::none_of(exts.begin(), exts.end(),
                             [](const VkExtensionProperties& e) { return std::string_view(e.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME; }))
                continue;
            uint32_t qCount = 0;
            vk.vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(qCount);
            vk.vkGetPhysicalDeviceQueueFamilyProperties(d, &qCount, families.data());
            for (uint32_t q = 0; q < qCount; ++q) {
                VkBool32 present = VK_FALSE;
                vk.vkGetPhysicalDeviceSurfaceSupportKHR(d, q, surface, &present);
                if (!(families[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;
                int rank = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                           : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2
                           : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU          ? 0
                                                                                  : 1;
                if (wanted && std::string_view(p.deviceName).find(wanted) != std::string_view::npos) rank = 10;
                if (rank > bestRank) {
                    bestRank = rank;
                    physical = d;
                    properties = p;
                    queueFamily = q;
                }
                break;
            }
        }
        return physical != VK_NULL_HANDLE;
    }

    Backend backend() const override { return Backend::Vulkan; }
    std::string deviceName() const override { return properties.deviceName; }
    int validationErrors() const override { return errors.load(); }
    bool clipYDown() const override { return true; }
    Format swapchainFormat() const override { return swapFormatKompoz; }

    // --- Resources ---------------------------------------------------------------------------

    std::unique_ptr<Texture> createTexture(int width, int height, Format format, uint8_t usage) override {
        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = vkFormat(format);
        ici.extent = {uint32_t(width), uint32_t(height), 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (usage & TextureUsage::Sampled) ici.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (usage & TextureUsage::RenderTarget) ici.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        if (vmaCreateImage(allocator, &ici, &aci, &image, &allocation, nullptr) != VK_SUCCESS) return nullptr;
        VkImageView view = makeView(image, ici.format);
        return std::make_unique<VulkanTexture>(*this, width, height, format, image, view, allocation);
    }

    VkImageView makeView(VkImage image, VkFormat format) {
        VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vci.image = image;
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = format;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        vk.vkCreateImageView(device, &vci, nullptr, &view);
        return view;
    }

    // Destroyed once the GPU is done with the frames that may use it.
    void destroyTexture(VulkanTexture& t) {
        VkImage image = t.image;
        VkImageView view = t.view;
        VmaAllocation allocation = t.allocation;
        auto framebuffers = std::move(t.framebuffers);
        later([this, image, view, allocation, framebuffers] {
            for (auto& [pass, fb] : framebuffers) vk.vkDestroyFramebuffer(device, fb, nullptr);
            vk.vkDestroyImageView(device, view, nullptr);
            if (allocation) vmaDestroyImage(allocator, image, allocation);
        });
        if (target == &t) target = nullptr;
        for (auto& [tex, sampler] : textures)
            if (tex == &t) tex = nullptr;
    }

    std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) override {
        auto module = [&](std::span<const unsigned char> code) {
            VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            smci.codeSize = code.size();
            smci.pCode = reinterpret_cast<const uint32_t*>(code.data());
            VkShaderModule m = VK_NULL_HANDLE;
            vk.vkCreateShaderModule(device, &smci, nullptr, &m);
            return m;
        };
        VkShaderModule vs = module(desc.vertex->spirv), fs = module(desc.fragment->spirv);
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, desc.vertex->entry, nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, desc.fragment->entry, nullptr};
        VkVertexInputBindingDescription binding{0, desc.instanceStride, VK_VERTEX_INPUT_RATE_INSTANCE};
        std::vector<VkVertexInputAttributeDescription> attributes;
        for (const VertexAttribute& a : desc.instanceAttributes) attributes.push_back({a.location, 0, vkVertexFormat(a.format), a.offset});
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = desc.instanceStride ? 1 : 0;
        vi.pVertexBindingDescriptions = &binding;
        vi.vertexAttributeDescriptionCount = uint32_t(attributes.size());
        vi.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1;
        vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};
        blend.blendEnable = desc.blend.enabled ? VK_TRUE : VK_FALSE;
        blend.srcColorBlendFactor = vkFactor(desc.blend.srcColor);
        blend.dstColorBlendFactor = vkFactor(desc.blend.dstColor);
        blend.colorBlendOp = vkOp(desc.blend.colorOp);
        blend.srcAlphaBlendFactor = vkFactor(desc.blend.srcAlpha);
        blend.dstAlphaBlendFactor = vkFactor(desc.blend.dstAlpha);
        blend.alphaBlendOp = vkOp(desc.blend.alphaOp);
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1;
        cb.pAttachments = &blend;
        std::array<VkDynamicState, 2> dynamics{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dyn.dynamicStateCount = uint32_t(dynamics.size());
        dyn.pDynamicStates = dynamics.data();
        VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gpci.stageCount = uint32_t(stages.size());
        gpci.pStages = stages.data();
        gpci.pVertexInputState = &vi;
        gpci.pInputAssemblyState = &ia;
        gpci.pViewportState = &vp;
        gpci.pRasterizationState = &rs;
        gpci.pMultisampleState = &ms;
        gpci.pColorBlendState = &cb;
        gpci.pDynamicState = &dyn;
        gpci.layout = pipelineLayout;
        gpci.renderPass = renderPass(vkFormat(desc.target), false);  // compatible with the clearing one too
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkResult r = vk.vkCreateGraphicsPipelines(device, pipelineCache, 1, &gpci, nullptr, &pipeline);
        vk.vkDestroyShaderModule(device, vs, nullptr);
        vk.vkDestroyShaderModule(device, fs, nullptr);
        if (r != VK_SUCCESS) return nullptr;
        return std::make_unique<VulkanPipeline>(*this, pipeline);
    }

    VkRenderPass renderPass(VkFormat format, bool clear) {
        auto [it, added] = renderPasses.try_emplace({format, clear}, VK_NULL_HANDLE);
        if (!added) return it->second;
        VkAttachmentDescription a{};
        a.format = format;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;
        VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpci.attachmentCount = 1;
        rpci.pAttachments = &a;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &sub;
        vk.vkCreateRenderPass(device, &rpci, nullptr, &it->second);
        return it->second;
    }

    VkSampler sampler(const Sampler& s) {
        for (auto& [desc, vs] : samplers)
            if (desc == s) return vs;
        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sci.magFilter = sci.minFilter = s.filter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = vkWrap(s.wrapU);
        sci.addressModeV = vkWrap(s.wrapV);
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        sci.maxLod = 0;
        VkSampler vs = VK_NULL_HANDLE;
        vk.vkCreateSampler(device, &sci, nullptr, &vs);
        samplers.emplace_back(s, vs);
        return vs;
    }

    // --- Swapchain ---------------------------------------------------------------------------

    void destroySwapchain() {
        for (auto& img : swapImages) {
            for (auto& [pass, fb] : img->framebuffers) vk.vkDestroyFramebuffer(device, fb, nullptr);
            img->framebuffers.clear();
            vk.vkDestroyImageView(device, img->view, nullptr);
            img->view = VK_NULL_HANDLE;
            img->allocation = VK_NULL_HANDLE;
        }
        swapImages.clear();
        for (VkSemaphore s : renderFinished) vk.vkDestroySemaphore(device, s, nullptr);
        renderFinished.clear();
        if (swapchain) vk.vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }

    // (Re)makes the swapchain at the drawable's size; false if it is 0 x 0 (minimized).
    bool makeSwapchain() {
        int w = 0, h = 0;
        if (drawableSize) drawableSize(&w, &h);
        VkSurfaceCapabilitiesKHR caps{};
        vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
        VkExtent2D extent = caps.currentExtent.width != UINT32_MAX
                                ? caps.currentExtent
                                : VkExtent2D{std::clamp(uint32_t(std::max(w, 0)), caps.minImageExtent.width, caps.maxImageExtent.width),
                                             std::clamp(uint32_t(std::max(h, 0)), caps.minImageExtent.height, caps.maxImageExtent.height)};
        if (extent.width == 0 || extent.height == 0) return false;
        vk.vkDeviceWaitIdle(device);
        // (Not this frame's: what it recorded may still use them.)
        for (int i = 0; i < kFramesInFlight; ++i)
            if (i != frameIndex) runDeletions(frames[size_t(i)]);
        VkSwapchainKHR old = swapchain;
        for (auto& img : swapImages) {
            for (auto& [pass, fb] : img->framebuffers) vk.vkDestroyFramebuffer(device, fb, nullptr);
            img->framebuffers.clear();
            vk.vkDestroyImageView(device, img->view, nullptr);
            img->view = VK_NULL_HANDLE;
        }
        swapImages.clear();
        for (VkSemaphore s : renderFinished) vk.vkDestroySemaphore(device, s, nullptr);
        renderFinished.clear();

        uint32_t count = 0;
        vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(count);
        vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data());
        VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : formats[0];
        for (const VkSurfaceFormatKHR& f : formats)  // UNORM: blending as Kompoz always has
            if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosen = f;
                break;
            }
        swapFormat = chosen.format;
        swapFormatKompoz = chosen.format == VK_FORMAT_R8G8B8A8_UNORM ? Format::RGBA8 : Format::BGRA8;
        VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        sci.surface = surface;
        sci.minImageCount = std::max(caps.minImageCount, 2u) + 1;
        if (caps.maxImageCount) sci.minImageCount = std::min(sci.minImageCount, caps.maxImageCount);
        sci.imageFormat = chosen.format;
        sci.imageColorSpace = chosen.colorSpace;
        sci.imageExtent = extent;
        sci.imageArrayLayers = 1;
        sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sci.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
        for (VkCompositeAlphaFlagBitsKHR a : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                              VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
            if (caps.supportedCompositeAlpha & a) {
                sci.compositeAlpha = a;
                break;
            }
        sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // vsync, always there
        sci.clipped = VK_TRUE;
        sci.oldSwapchain = old;
        VkSwapchainKHR made = VK_NULL_HANDLE;
        VkResult r = vk.vkCreateSwapchainKHR(device, &sci, nullptr, &made);
        if (old) vk.vkDestroySwapchainKHR(device, old, nullptr);
        swapchain = VK_NULL_HANDLE;
        if (r != VK_SUCCESS) {
            log("kompoz: vkCreateSwapchainKHR failed (" + std::to_string(int(r)) + ")");
            return false;
        }
        swapchain = made;
        swapExtent = extent;
        vk.vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
        std::vector<VkImage> images(count);
        vk.vkGetSwapchainImagesKHR(device, swapchain, &count, images.data());
        for (VkImage image : images) {
            swapImages.push_back(std::make_unique<VulkanTexture>(*this, int(extent.width), int(extent.height), swapFormatKompoz, image,
                                                                 makeView(image, chosen.format), VK_NULL_HANDLE));
            VkSemaphoreCreateInfo semci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            VkSemaphore s = VK_NULL_HANDLE;
            vk.vkCreateSemaphore(device, &semci, nullptr, &s);
            renderFinished.push_back(s);
        }
        return true;
    }

    // --- Frames ------------------------------------------------------------------------------

    void later(std::function<void()> f) { frames[size_t(frameIndex)].deletions.push_back(std::move(f)); }
    void runDeletions(Frame& f) {
        for (auto& d : f.deletions) d();
        f.deletions.clear();
    }

    void beginFrame() override {
        frameIndex = (frameIndex + 1) % kFramesInFlight;
        Frame& f = frames[size_t(frameIndex)];
        vk.vkWaitForFences(device, 1, &f.fence, VK_TRUE, UINT64_MAX);
        runDeletions(f);
        vk.vkResetCommandPool(device, f.pool, 0);
        f.nextBuffer = 0;
        vk.vkResetDescriptorPool(device, f.descriptors, 0);
        for (Chunk& c : f.chunks) c.used = 0;
        inFrame = true;
        acquired = false;
        waitImage = false;
        startCommands();
    }

    void startCommands() {
        Frame& f = frames[size_t(frameIndex)];
        if (f.nextBuffer == f.buffers.size()) {
            VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            cbai.commandPool = f.pool;
            cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cbai.commandBufferCount = 1;
            VkCommandBuffer b = VK_NULL_HANDLE;
            vk.vkAllocateCommandBuffers(device, &cbai, &b);
            f.buffers.push_back(b);
        }
        cmd = f.buffers[f.nextBuffer++];
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vk.vkBeginCommandBuffer(cmd, &bi);
        boundPipeline = VK_NULL_HANDLE;
        texturesDirty = true;
    }

    Texture* swapchainTexture() override {
        if (!acquired) {
            int w = 0, h = 0;
            if (drawableSize) drawableSize(&w, &h);
            if (w <= 0 || h <= 0) return nullptr;  // minimized
            if (!swapchain || int(swapExtent.width) != w || int(swapExtent.height) != h)
                if (!makeSwapchain()) return nullptr;
            Frame& f = frames[size_t(frameIndex)];
            VkResult r = vk.vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, f.imageAvailable, VK_NULL_HANDLE, &swapIndex);
            if (r == VK_ERROR_OUT_OF_DATE_KHR) {  // resized meanwhile: made again at the next frame
                vk.vkDestroySwapchainKHR(device, std::exchange(swapchain, VK_NULL_HANDLE), nullptr);
            }
            acquired = r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR;
            waitImage = acquired;
            if (acquired) swapImages[swapIndex]->layout = VK_IMAGE_LAYOUT_UNDEFINED;  // presented before: its pixels don't matter
        }
        return acquired ? swapImages[swapIndex].get() : nullptr;
    }

    void* allocate(size_t size, size_t alignment, BufferRef* ref) override {
        Frame& f = frames[size_t(frameIndex)];
        alignment = std::max<size_t>(alignment, 16);
        for (Chunk& c : f.chunks) {
            VkDeviceSize start = (c.used + alignment - 1) / alignment * alignment;
            if (start + size <= c.size) {
                c.used = start + size;
                *ref = {c.buffer, start};
                return c.mapped + start;
            }
        }
        Chunk c;
        c.size = std::max<VkDeviceSize>(kChunkSize, size);
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = c.size;
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo info{};
        if (vmaCreateBuffer(allocator, &bci, &aci, &c.buffer, &c.allocation, &info) != VK_SUCCESS) return nullptr;
        c.mapped = static_cast<uint8_t*>(info.pMappedData);
        c.used = size;
        f.chunks.push_back(c);
        *ref = {c.buffer, 0};
        return c.mapped;
    }

    // The pipeline stage and access a layout's users have, for barriers.
    static void stageOf(VkImageLayout layout, VkPipelineStageFlags& stage, VkAccessFlags& access) {
        switch (layout) {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: stage = VK_PIPELINE_STAGE_TRANSFER_BIT, access = VK_ACCESS_TRANSFER_WRITE_BIT; break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: stage = VK_PIPELINE_STAGE_TRANSFER_BIT, access = VK_ACCESS_TRANSFER_READ_BIT; break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL: stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, access = VK_ACCESS_SHADER_READ_BIT; break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, access = 0; break;
        default: stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, access = 0; break;
        }
    }

    // Moves a texture to `layout`, waiting for what used it before (always: a texture drawn into
    // twice in a row is waited on too).
    void transition(VulkanTexture& t, VkImageLayout layout) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        VkPipelineStageFlags srcStage, dstStage;
        stageOf(t.layout, srcStage, b.srcAccessMask);
        stageOf(layout, dstStage, b.dstAccessMask);
        b.oldLayout = t.layout;
        b.newLayout = layout;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = t.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vk.vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
        t.layout = layout;
    }

    void upload(Texture& texture, const IRect& region, const void* pixels, size_t rowBytes) override {
        auto& t = static_cast<VulkanTexture&>(texture);
        size_t row = size_t(region.width) * 4, size = row * size_t(region.height);
        BufferRef ref;
        auto* dst = static_cast<uint8_t*>(allocate(size, 16, &ref));
        if (!dst) return;
        bool swap = t.format() == Format::BGRA8;
        for (int y = 0; y < region.height; ++y) {
            const auto* src = static_cast<const uint8_t*>(pixels) + size_t(y) * rowBytes;
            uint8_t* out = dst + size_t(y) * row;
            if (!swap) {
                std::memcpy(out, src, row);
            } else {
                for (int x = 0; x < region.width; ++x)
                    out[x * 4] = src[x * 4 + 2], out[x * 4 + 1] = src[x * 4 + 1], out[x * 4 + 2] = src[x * 4], out[x * 4 + 3] = src[x * 4 + 3];
            }
        }
        transition(t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.bufferOffset = ref.offset;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageOffset = {region.x, region.y, 0};
        copy.imageExtent = {uint32_t(region.width), uint32_t(region.height), 1};
        vk.vkCmdCopyBufferToImage(cmd, VkBuffer(const_cast<void*>(ref.buffer)), t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    }

    void beginPass(Texture& texture, const std::optional<ClearColor>& clear, std::span<Texture* const> reads) override {
        for (Texture* r : reads)
            if (r && r != &texture) transition(static_cast<VulkanTexture&>(*r), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        auto& t = static_cast<VulkanTexture&>(texture);
        transition(t, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderPass pass = renderPass(vkFormat(t.format()), clear.has_value());
        VkFramebuffer fb = VK_NULL_HANDLE;
        for (auto& [p, f] : t.framebuffers)
            if (p == pass) fb = f;
        if (!fb) {
            VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fci.renderPass = pass;
            fci.attachmentCount = 1;
            fci.pAttachments = &t.view;
            fci.width = uint32_t(t.width());
            fci.height = uint32_t(t.height());
            fci.layers = 1;
            vk.vkCreateFramebuffer(device, &fci, nullptr, &fb);
            t.framebuffers.emplace_back(pass, fb);
        }
        VkClearValue cv{};
        if (clear) cv.color = {{clear->r, clear->g, clear->b, clear->a}};
        VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rbi.renderPass = pass;
        rbi.framebuffer = fb;
        rbi.renderArea = {{0, 0}, {uint32_t(t.width()), uint32_t(t.height())}};
        rbi.clearValueCount = clear ? 1 : 0;
        rbi.pClearValues = &cv;
        vk.vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{0, 0, float(t.width()), float(t.height()), 0, 1};
        vk.vkCmdSetViewport(cmd, 0, 1, &viewport);
        setScissor({0, 0, t.width(), t.height()});
        target = &t;
        texturesDirty = true;
    }

    void setPipeline(Pipeline& pipeline) override {
        VkPipeline p = static_cast<VulkanPipeline&>(pipeline).pipeline;
        if (p == boundPipeline) return;
        vk.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
        boundPipeline = p;
    }

    void setScissor(const IRect& r) override {
        int x0 = std::max(0, r.x), y0 = std::max(0, r.y);
        int x1 = target ? std::min(r.x + r.width, target->width()) : r.x + r.width;
        int y1 = target ? std::min(r.y + r.height, target->height()) : r.y + r.height;
        VkRect2D rect{{x0, y0}, {uint32_t(std::max(0, x1 - x0)), uint32_t(std::max(0, y1 - y0))}};
        vk.vkCmdSetScissor(cmd, 0, 1, &rect);
    }

    void setUniforms(const void* data, size_t size) override {
        vk.vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                              uint32_t(std::min(size, kMaxUniforms)), data);
    }

    void setTexture(int slot, Texture& texture, const Sampler& s) override {
        auto& entry = textures[size_t(slot)];
        if (entry.first == &texture && entry.second == s) return;
        entry = {static_cast<VulkanTexture*>(&texture), s};
        texturesDirty = true;
    }

    void bindTextures() {
        Frame& f = frames[size_t(frameIndex)];
        VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dsai.descriptorPool = f.descriptors;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &setLayout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (vk.vkAllocateDescriptorSets(device, &dsai, &set) != VK_SUCCESS) return;
        std::array<VkDescriptorImageInfo, kMaxTextures> infos{};
        std::array<VkWriteDescriptorSet, kMaxTextures> writes{};
        uint32_t n = 0;
        for (int i = 0; i < kMaxTextures; ++i) {
            auto& [tex, s] = textures[size_t(i)];
            VulkanTexture* t = tex ? tex : textures[0].first;  // unused bindings: any valid texture
            if (!t) continue;
            infos[n] = {sampler(tex ? s : textures[0].second), t->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            writes[n] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, uint32_t(i + 1), 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                         &infos[n], nullptr, nullptr};
            ++n;
        }
        if (n < kMaxTextures) return;  // nothing bound yet
        vk.vkUpdateDescriptorSets(device, n, writes.data(), 0, nullptr);
        vk.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &set, 0, nullptr);
        texturesDirty = false;
    }

    void draw(const BufferRef& instances, uint32_t vertexCount, uint32_t instanceCount) override {
        if (instanceCount == 0) return;
        if (texturesDirty) bindTextures();
        VkBuffer buffer = VkBuffer(const_cast<void*>(instances.buffer));
        VkDeviceSize offset = instances.offset;
        if (buffer) vk.vkCmdBindVertexBuffers(cmd, 0, 1, &buffer, &offset);
        vk.vkCmdDraw(cmd, vertexCount, instanceCount, 0, 0);
    }

    void endPass() override {
        vk.vkCmdEndRenderPass(cmd);
        target = nullptr;
    }

    // Submits what was recorded so far (waiting for the swapchain image the first time, and
    // signaling the semaphore presenting waits for at the last).
    void submit(VkFence fence, bool present) {
        vk.vkEndCommandBuffer(cmd);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        Frame& f = frames[size_t(frameIndex)];
        if (waitImage) {
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores = &f.imageAvailable;
            si.pWaitDstStageMask = &waitStage;
            waitImage = false;
        }
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        if (present) {
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores = &renderFinished[swapIndex];
        }
        vk.vkQueueSubmit(queue, 1, &si, fence);
    }

    std::vector<uint8_t> readPixels(Texture& texture, const IRect& region) override {
        auto& t = static_cast<VulkanTexture&>(texture);
        VkDeviceSize size = VkDeviceSize(region.width) * VkDeviceSize(region.height) * 4;
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = size;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo aci{};
        aci.usage = VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VmaAllocationInfo info{};
        if (vmaCreateBuffer(allocator, &bci, &aci, &buffer, &allocation, &info) != VK_SUCCESS) return {};
        VkImageLayout before = t.layout;
        transition(t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageOffset = {region.x, region.y, 0};
        copy.imageExtent = {uint32_t(region.width), uint32_t(region.height), 1};
        vk.vkCmdCopyImageToBuffer(cmd, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
        if (before != VK_IMAGE_LAYOUT_UNDEFINED) transition(t, before);
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        vk.vkCreateFence(device, &fci, nullptr, &fence);
        submit(fence, false);
        vk.vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vk.vkDestroyFence(device, fence, nullptr);
        vmaInvalidateAllocation(allocator, allocation, 0, VK_WHOLE_SIZE);
        std::vector<uint8_t> out(static_cast<size_t>(size));
        std::memcpy(out.data(), info.pMappedData, out.size());
        vmaDestroyBuffer(allocator, buffer, allocation);
        if (t.format() == Format::BGRA8)
            for (size_t i = 0; i < out.size(); i += 4) std::swap(out[i], out[i + 2]);
        startCommands();  // the rest of the frame
        return out;
    }

    void endFrame(bool present) override {
        if (!inFrame) return;
        present = present && acquired;
        if (present) transition(*swapImages[swapIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        Frame& f = frames[size_t(frameIndex)];
        vk.vkResetFences(device, 1, &f.fence);
        if (acquired && !present) {
            // An image was acquired but isn't shown: it must still be given back, so it is
            // presented as drawn (the window gets it only if something was).
            transition(*swapImages[swapIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
            present = true;
        }
        submit(f.fence, present);
        if (present) {
            VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = &renderFinished[swapIndex];
            pi.swapchainCount = 1;
            pi.pSwapchains = &swapchain;
            pi.pImageIndices = &swapIndex;
            VkResult r = vk.vkQueuePresentKHR(queue, &pi);
            if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) swapExtent = {};  // made again next frame
        }
        inFrame = false;
        acquired = false;
    }

    void waitIdle() override {
        if (device) vk.vkDeviceWaitIdle(device);
    }
};

VulkanTexture::~VulkanTexture() {
    if (allocation) device_.destroyTexture(*this);
}

VulkanPipeline::~VulkanPipeline() {
    VkPipeline p = pipeline;
    VulkanDevice& d = device_;
    d.later([&d, p] { d.vk.vkDestroyPipeline(d.device, p, nullptr); });
}

} // namespace

std::unique_ptr<Device> createVulkanDevice(const Surface& surface, std::string* error) {
    auto device = std::make_unique<VulkanDevice>();
    if (!device->init(surface, error)) return nullptr;
    return device;
}

} // namespace kompoz::gpu
