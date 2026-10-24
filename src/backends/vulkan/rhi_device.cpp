/** @file @brief Vulkan 1.3 RHI：动态渲染、同步、资源与描述符生命周期以及窗口呈现。 */
#include "volk.h" // 必须先定义 Vulkan 类型，再包含 GLFW 的内部窗口桥。

#include "../../internal/pool.hpp"
#include "../../internal/rhi_backend.hpp"
#include "../../internal/window.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

namespace renderer::rhi {
namespace {
void verify(bool condition, const char *message) {
    if (!condition)
        throw std::invalid_argument(message);
}
void vkCheck(VkResult result, const char *operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": VkResult " + std::to_string(result));
}
void range(size_t capacity, size_t offset, size_t size) {
    verify(offset <= capacity && size <= capacity - offset, "Buffer/image range out of bounds");
}
VkFormat vkFormat(Format format) {
    switch (format) {
    case Format::Rgba8:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::Rgba8Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::Rgba16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::Rgba32Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::R32Float:
        return VK_FORMAT_R32_SFLOAT;
    case Format::Depth32Float:
        return VK_FORMAT_D32_SFLOAT;
    }
    throw std::invalid_argument("Invalid texture format");
}
size_t pixelBytes(Format format) {
    switch (format) {
    case Format::Rgba16Float:
        return 8;
    case Format::Rgba32Float:
        return 16;
    default:
        return 4;
    }
}
VkCompareOp vkCompare(Compare value) {
    switch (value) {
    case Compare::Never:
        return VK_COMPARE_OP_NEVER;
    case Compare::Less:
        return VK_COMPARE_OP_LESS;
    case Compare::LessEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case Compare::Equal:
        return VK_COMPARE_OP_EQUAL;
    case Compare::Greater:
        return VK_COMPARE_OP_GREATER;
    case Compare::Always:
        return VK_COMPARE_OP_ALWAYS;
    }
    throw std::invalid_argument("Invalid depth comparison");
}
VkSamplerAddressMode vkAddress(AddressMode value) {
    switch (value) {
    case AddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case AddressMode::Clamp:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case AddressMode::Mirror:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case AddressMode::Border:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    throw std::invalid_argument("Invalid address mode");
}
VkBlendFactor vkBlend(BlendFactor value) {
    switch (value) {
    case BlendFactor::Zero:
        return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One:
        return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SourceColor:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::OneMinusSourceColor:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::SourceAlpha:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSourceAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DestinationColor:
        return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::OneMinusDestinationColor:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case BlendFactor::DestinationAlpha:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::OneMinusDestinationAlpha:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    }
    throw std::invalid_argument("Invalid blend factor");
}
VkDescriptorType descriptorType(BindingKind value) {
    switch (value) {
    case BindingKind::UniformBuffer:
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingKind::StorageBuffer:
        return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingKind::SampledTexture:
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case BindingKind::StorageImage:
        return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    throw std::invalid_argument("Invalid descriptor kind");
}
VkPipelineStageFlags2 stages(Stage value) {
    if (value == Stage::All)
        return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    const auto bits = static_cast<uint32_t>(value);
    VkPipelineStageFlags2 result = 0;
    if (bits & uint32_t(Stage::Vertex))
        result |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    if (bits & uint32_t(Stage::Fragment))
        result |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    if (bits & uint32_t(Stage::Compute))
        result |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    if (bits & uint32_t(Stage::Transfer))
        result |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    if (bits & uint32_t(Stage::Indirect))
        result |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    if (bits & uint32_t(Stage::Attachment))
        result |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    return result;
}
VkAccessFlags2 accessMask(Access access) {
    switch (access) {
    case Access::None:
        return VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    case Access::VertexRead:
        return VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
    case Access::IndexRead:
        return VK_ACCESS_2_INDEX_READ_BIT;
    case Access::UniformRead:
        return VK_ACCESS_2_UNIFORM_READ_BIT;
    case Access::SampledRead:
        return VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    case Access::StorageRead:
        return VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
    case Access::StorageWrite:
        return VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    case Access::StorageReadWrite:
        return VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    case Access::IndirectRead:
        return VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
    case Access::TransferRead:
    case Access::Present:
        return VK_ACCESS_2_TRANSFER_READ_BIT;
    case Access::TransferWrite:
        return VK_ACCESS_2_TRANSFER_WRITE_BIT;
    case Access::ColorWrite:
        return VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    case Access::DepthWrite:
        return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
               VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    case Access::DepthRead:
        return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    }
    throw std::invalid_argument("Invalid resource access");
}

// 每个设备持有独立 dispatch table，不将某台设备的函数指针写入进程全局表。
struct Context {
    VolkInstanceTable i{};
    VolkDeviceTable f{};
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE, presentQueue = VK_NULL_HANDLE;
    uint32_t family = 0, presentFamily = 0, timestampBits = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory{};
    VkPhysicalDeviceFeatures features{};
    std::atomic<uint32_t> errors{0};
    ~Context() {
        if (device) {
            f.vkDeviceWaitIdle(device);
            if (pipelineCache)
                f.vkDestroyPipelineCache(device, pipelineCache, nullptr);
            if (commandPool)
                f.vkDestroyCommandPool(device, commandPool, nullptr);
            f.vkDestroyDevice(device, nullptr);
        }
        if (surface)
            i.vkDestroySurfaceKHR(instance, surface, nullptr);
        if (messenger)
            i.vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
        if (instance)
            i.vkDestroyInstance(instance, nullptr);
    }
    uint32_t memoryType(uint32_t allowed, VkMemoryPropertyFlags required) const {
        for (uint32_t index = 0; index < memory.memoryTypeCount; ++index)
            if ((allowed & (1u << index)) &&
                (memory.memoryTypes[index].propertyFlags & required) == required)
                return index;
        throw std::runtime_error("Vulkan has no suitable memory type");
    }
};
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT *data,
                                             void *user) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++static_cast<Context *>(user)->errors;
        std::cerr << "Vulkan validation: " << data->pMessage << '\n';
    }
    return VK_FALSE;
}
std::shared_ptr<Context> initialize(GLFWwindow *window, bool debug) {
    static std::once_flag loader;
    std::call_once(loader, [] { vkCheck(volkInitialize(), "Load Vulkan runtime"); });
    auto c = std::make_shared<Context>();
    verify(glfwVulkanSupported() == GLFW_TRUE, "Vulkan loader or driver is unavailable");
    uint32_t count = 0;
    auto required = glfwGetRequiredInstanceExtensions(&count);
    verify(required && count, "GLFW cannot create a Vulkan surface");
    std::vector<const char *> extensions(required, required + count);
    std::vector<const char *> layers;
    std::vector<VkLayerProperties> layerProperties;
    vkCheck(vkEnumerateInstanceLayerProperties(&count, nullptr), "Enumerate Vulkan layers");
    layerProperties.resize(count);
    vkCheck(vkEnumerateInstanceLayerProperties(&count, layerProperties.data()),
            "Enumerate Vulkan layers");
    bool validation =
        debug && std::any_of(layerProperties.begin(), layerProperties.end(), [](const auto &p) {
            return std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0;
        });
    if (validation)
        layers.push_back("VK_LAYER_KHRONOS_validation");
    vkCheck(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr),
            "Enumerate Vulkan extensions");
    std::vector<VkExtensionProperties> extensionProperties(count);
    vkCheck(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensionProperties.data()),
            "Enumerate Vulkan extensions");
    bool utils =
        debug &&
        std::any_of(extensionProperties.begin(), extensionProperties.end(), [](const auto &p) {
            return std::strcmp(p.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
        });
    if (utils)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    VkDebugUtilsMessengerCreateInfoEXT message{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    message.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    message.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    message.pfnUserCallback = debugCallback;
    message.pUserData = c.get();
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Renderer";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo instance{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance.pApplicationInfo = &app;
    instance.enabledExtensionCount = uint32_t(extensions.size());
    instance.ppEnabledExtensionNames = extensions.data();
    instance.enabledLayerCount = uint32_t(layers.size());
    instance.ppEnabledLayerNames = layers.data();
    instance.pNext = utils ? &message : nullptr;
    VkValidationFeatureEnableEXT syncFeature =
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validationFeatures{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    if (validation) {
        vkCheck(
            vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &count, nullptr),
            "Query validation extensions");
        std::vector<VkExtensionProperties> validationExtensions(count);
        vkCheck(vkEnumerateInstanceExtensionProperties("VK_LAYER_KHRONOS_validation", &count,
                                                       validationExtensions.data()),
                "Query validation extensions");
        if (std::any_of(validationExtensions.begin(), validationExtensions.end(),
                        [](const auto &p) {
                            return std::strcmp(p.extensionName,
                                               VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) == 0;
                        })) {
            extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
            instance.enabledExtensionCount = uint32_t(extensions.size());
            instance.ppEnabledExtensionNames = extensions.data();
            validationFeatures.pNext = instance.pNext;
            validationFeatures.enabledValidationFeatureCount = 1;
            validationFeatures.pEnabledValidationFeatures = &syncFeature;
            instance.pNext = &validationFeatures;
        }
    }
    vkCheck(vkCreateInstance(&instance, nullptr, &c->instance), "Create Vulkan instance");
    volkLoadInstanceTable(&c->i, c->instance);
    if (utils)
        vkCheck(c->i.vkCreateDebugUtilsMessengerEXT(c->instance, &message, nullptr, &c->messenger),
                "Create Vulkan debug messenger");
    vkCheck(glfwCreateWindowSurface(c->instance, window, nullptr, &c->surface),
            "Create Vulkan surface");
    vkCheck(c->i.vkEnumeratePhysicalDevices(c->instance, &count, nullptr), "Enumerate Vulkan GPUs");
    std::vector<VkPhysicalDevice> devices(count);
    vkCheck(c->i.vkEnumeratePhysicalDevices(c->instance, &count, devices.data()),
            "Enumerate Vulkan GPUs");
    int best = -1;
    for (auto physical : devices) {
        VkPhysicalDeviceProperties properties;
        c->i.vkGetPhysicalDeviceProperties(physical, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3)
            continue;
        VkPhysicalDeviceVulkan13Features features13{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                           &features13};
        c->i.vkGetPhysicalDeviceFeatures2(physical, &features);
        if (!features13.dynamicRendering || !features13.synchronization2 ||
            !features.features.independentBlend)
            continue;
        c->i.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> deviceExtensions(count);
        c->i.vkEnumerateDeviceExtensionProperties(physical, nullptr, &count,
                                                  deviceExtensions.data());
        if (std::none_of(deviceExtensions.begin(), deviceExtensions.end(), [](const auto &e) {
                return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            }))
            continue;
        c->i.vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        c->i.vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues.data());
        std::optional<uint32_t> graphics, present;
        for (uint32_t index = 0; index < count; ++index) {
            VkBool32 supported = VK_FALSE;
            c->i.vkGetPhysicalDeviceSurfaceSupportKHR(physical, index, c->surface, &supported);
            if ((queues[index].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                    (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT) &&
                !graphics)
                graphics = index;
            if (supported && !present)
                present = index;
            if (supported && graphics && *graphics == index)
                present = index;
        }
        if (!graphics || !present || !queues[*graphics].timestampValidBits)
            continue;
        int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
        if (score <= best)
            continue;
        best = score;
        c->physical = physical;
        c->family = *graphics;
        c->presentFamily = *present;
        c->timestampBits = queues[*graphics].timestampValidBits;
        c->features = features.features;
        c->properties = properties;
    }
    verify(c->physical != VK_NULL_HANDLE, "Vulkan 1.3 with graphics/compute, dynamic rendering, "
                                          "timestamps and independent blend is required");
    const float priority = 1;
    std::vector<VkDeviceQueueCreateInfo> queues;
    for (auto family : std::set<uint32_t>{c->family, c->presentFamily}) {
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        q.queueFamilyIndex = family;
        q.queueCount = 1;
        q.pQueuePriorities = &priority;
        queues.push_back(q);
    }
    VkPhysicalDeviceVulkan13Features features13{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures enabled{};
    enabled.independentBlend = VK_TRUE;
    enabled.multiDrawIndirect = c->features.multiDrawIndirect;
    enabled.drawIndirectFirstInstance = c->features.drawIndirectFirstInstance;
    enabled.fragmentStoresAndAtomics = c->features.fragmentStoresAndAtomics;
    enabled.vertexPipelineStoresAndAtomics = c->features.vertexPipelineStoresAndAtomics;
    enabled.shaderStorageImageExtendedFormats = c->features.shaderStorageImageExtendedFormats;
    enabled.robustBufferAccess = c->features.robustBufferAccess;
    const char *swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device.pNext = &features13;
    device.pEnabledFeatures = &enabled;
    device.queueCreateInfoCount = uint32_t(queues.size());
    device.pQueueCreateInfos = queues.data();
    device.enabledExtensionCount = 1;
    device.ppEnabledExtensionNames = &swapchainExtension;
    vkCheck(c->i.vkCreateDevice(c->physical, &device, nullptr, &c->device), "Create Vulkan device");
    volkLoadDeviceTable(&c->f, c->device);
    c->f.vkGetDeviceQueue(c->device, c->family, 0, &c->queue);
    c->f.vkGetDeviceQueue(c->device, c->presentFamily, 0, &c->presentQueue);
    c->i.vkGetPhysicalDeviceMemoryProperties(c->physical, &c->memory);
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = c->family;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCheck(c->f.vkCreateCommandPool(c->device, &pool, nullptr, &c->commandPool),
            "Create Vulkan command pool");
    VkPipelineCacheCreateInfo cache{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    vkCheck(c->f.vkCreatePipelineCache(c->device, &cache, nullptr, &c->pipelineCache),
            "Create Vulkan pipeline cache");
    return c;
}

struct Native {
    enum Kind { Buffer, Storage, View, Sampler, Pipeline, Query, Shader } kind;
    std::shared_ptr<Context> c;
    uint64_t lastUse = 0;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void *mapping = nullptr;
    VkSampler sampler = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> sets;
    VkQueryPool query = VK_NULL_HANDLE;
    VkShaderModule shader = VK_NULL_HANDLE;
    std::shared_ptr<Native> parent;
    Native(Kind kind, std::shared_ptr<Context> context) : kind(kind), c(std::move(context)) {}
    ~Native() {
        auto &f = c->f;
        auto device = c->device;
        if (pipeline)
            f.vkDestroyPipeline(device, pipeline, nullptr);
        if (layout)
            f.vkDestroyPipelineLayout(device, layout, nullptr);
        for (auto set : sets)
            f.vkDestroyDescriptorSetLayout(device, set, nullptr);
        if (shader)
            f.vkDestroyShaderModule(device, shader, nullptr);
        if (sampler)
            f.vkDestroySampler(device, sampler, nullptr);
        if (query)
            f.vkDestroyQueryPool(device, query, nullptr);
        if (view)
            f.vkDestroyImageView(device, view, nullptr);
        if (mapping)
            f.vkUnmapMemory(device, memory);
        if (buffer)
            f.vkDestroyBuffer(device, buffer, nullptr);
        if (image)
            f.vkDestroyImage(device, image, nullptr);
        if (memory)
            f.vkFreeMemory(device, memory, nullptr);
    }
};
struct Buffer {
    std::shared_ptr<Native> native;
    BufferDesc desc;
};
struct Texture {
    std::shared_ptr<Native> native;
    TextureDesc desc;
    uint32_t baseMip = 0;
    std::map<uint32_t, std::shared_ptr<Native>> singleMipViews;
};
struct Sampler {
    std::shared_ptr<Native> native;
};
struct Pipeline {
    std::shared_ptr<Native> native;
    GraphicsPipelineDesc desc;
    bool compute = false;
    std::vector<ShaderBinding> bindings;
};
struct Queries {
    std::shared_ptr<Native> native;
    std::vector<bool> written;
};
struct Submission {
    uint64_t serial = 0;
    VkFence fence = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> descriptors;
    std::vector<std::shared_ptr<Native>> resources;
    VkSemaphore acquired = VK_NULL_HANDLE;
};
float halfFloat(uint16_t h) {
    const uint32_t sign = uint32_t(h & 0x8000) << 16;
    uint32_t exponent = (h >> 10) & 31, mantissa = h & 1023;
    if (exponent == 0) {
        if (!mantissa)
            return std::bit_cast<float>(sign);
        return (sign ? -1.f : 1.f) * std::ldexp(float(mantissa), -24);
    }
    return std::bit_cast<float>(sign | ((exponent == 31 ? 255 : exponent + 112) << 23) |
                                (mantissa << 13));
}
} // namespace

struct VulkanDevice final : detail::DeviceBackend {
    std::shared_ptr<renderer::detail::WindowState> window;
    std::shared_ptr<Context> c;
    std::thread::id thread = std::this_thread::get_id();
    uint64_t owner = renderer::detail::uniqueId(), submitted = 0, completed = 0;
    DeviceCaps caps;
    renderer::detail::Pool<Buffer, BufferTag> buffers;
    renderer::detail::Pool<Texture, TextureTag> textures;
    renderer::detail::Pool<Sampler, SamplerTag> samplers;
    renderer::detail::Pool<Pipeline, PipelineTag> pipelines;
    renderer::detail::Pool<BindingGroupDesc, GroupTag> groups;
    renderer::detail::Pool<Queries, QueryTag> queries;
    std::deque<Submission> submissions;
    std::vector<std::shared_ptr<Native>> retired;
    std::optional<Texture> presentSource;
    TextureHandle encodedPresent, linearPresent;
    uint32_t presentWidth = 0, presentHeight = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkExtent2D extent{};
    VkFormat swapFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> swapImages;
    std::vector<VkSemaphore> presentSemaphores;
    std::vector<bool> swapInitialized;
    bool attached = false;

    VulkanDevice(std::shared_ptr<renderer::detail::WindowState> state, bool debug)
        : window(std::move(state)) {
        window->checkThread();
        verify(window->graphicsApi == GraphicsApi::Vulkan, "Vulkan requires a Vulkan window");
        verify(!window->rendererAttached, "Window already has a graphics device");
        c = initialize(window->native, debug);
        const auto &p = c->properties;
        const auto &l = p.limits;
        caps.backend = "Vulkan";
        caps.device = p.deviceName;
        caps.vendor = p.vendorID == 0x10de   ? "NVIDIA"
                      : p.vendorID == 0x1002 ? "AMD"
                      : p.vendorID == 0x8086 ? "Intel"
                                             : std::to_string(p.vendorID);
        caps.version = std::to_string(VK_API_VERSION_MAJOR(p.apiVersion)) + "." +
                       std::to_string(VK_API_VERSION_MINOR(p.apiVersion));
        caps.maxTextureSize = l.maxImageDimension2D;
        caps.maxColorAttachments = l.maxColorAttachments;
        caps.maxVertexAttributes = l.maxVertexInputAttributes;
        caps.maxVertexStride = l.maxVertexInputBindingStride;
        caps.maxSampledTextures = l.maxDescriptorSetSampledImages;
        caps.maxUniformBindings = l.maxDescriptorSetUniformBuffers;
        caps.maxStorageBindings = l.maxDescriptorSetStorageBuffers;
        caps.maxImageBindings = l.maxDescriptorSetStorageImages;
        caps.uniformAlignment = uint32_t(l.minUniformBufferOffsetAlignment);
        caps.storageAlignment = uint32_t(l.minStorageBufferOffsetAlignment);
        caps.maxComputeInvocations = l.maxComputeWorkGroupInvocations;
        caps.indirectDraw = c->features.drawIndirectFirstInstance == VK_TRUE;
        window->rendererAttached = true;
        attached = true;
    }
    ~VulkanDevice() override {
        if (c) {
            c->f.vkDeviceWaitIdle(c->device);
            while (!submissions.empty()) {
                release(submissions.front());
                submissions.pop_front();
            }
            retired.clear();
            presentSource.reset();
            destroySwapchain();
        }
        if (attached) {
            window->presentCallback = {};
            window->rendererAttached = false;
        }
    }
    void checkThread() const {
        if (thread != std::this_thread::get_id())
            throw std::logic_error("Vulkan resource operations must use the device thread");
    }
    DeviceCaps capabilities() const override {
        checkThread();
        return caps;
    }
    FormatFeatures formatFeatures(Format format) const override {
        checkThread();
        VkFormatProperties p;
        c->i.vkGetPhysicalDeviceFormatProperties(c->physical, vkFormat(format), &p);
        const auto flags = p.optimalTilingFeatures;
        return {bool(flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT),
                bool(flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT),
                bool(flags & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT),
                bool(flags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)};
    }
    void release(Submission &s) {
        for (auto pool : s.descriptors)
            c->f.vkDestroyDescriptorPool(c->device, pool, nullptr);
        if (s.command)
            c->f.vkFreeCommandBuffers(c->device, c->commandPool, 1, &s.command);
        if (s.fence)
            c->f.vkDestroyFence(c->device, s.fence, nullptr);
        if (s.acquired)
            c->f.vkDestroySemaphore(c->device, s.acquired, nullptr);
        s.resources.clear();
    }
    void collect() {
        while (!submissions.empty()) {
            auto status = c->f.vkGetFenceStatus(c->device, submissions.front().fence);
            if (status == VK_NOT_READY)
                break;
            vkCheck(status, "Query Vulkan fence");
            completed = submissions.front().serial;
            release(submissions.front());
            submissions.pop_front();
        }
        std::erase_if(retired, [&](const auto &native) { return native->lastUse <= completed; });
    }
    Submission begin() {
        Submission s;
        s.serial = submitted + 1;
        VkCommandBufferAllocateInfo a{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        a.commandPool = c->commandPool;
        a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        a.commandBufferCount = 1;
        vkCheck(c->f.vkAllocateCommandBuffers(c->device, &a, &s.command),
                "Allocate Vulkan command buffer");
        VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        b.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        try {
            vkCheck(c->f.vkBeginCommandBuffer(s.command, &b), "Begin Vulkan command buffer");
        } catch (...) {
            release(s);
            throw;
        }
        return s;
    }
    void use(Submission &s, const std::shared_ptr<Native> &native) {
        s.resources.push_back(native);
        if (native->parent)
            use(s, native->parent);
    }
    CompletionToken queue(Submission s, VkSemaphore wait = VK_NULL_HANDLE,
                          VkSemaphore signal = VK_NULL_HANDLE) {
        try {
            vkCheck(c->f.vkEndCommandBuffer(s.command), "End Vulkan command buffer");
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            vkCheck(c->f.vkCreateFence(c->device, &fence, nullptr, &s.fence),
                    "Create Vulkan fence");
            VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &s.command;
            submit.waitSemaphoreCount = wait ? 1u : 0u;
            submit.pWaitSemaphores = &wait;
            submit.pWaitDstStageMask = &waitStage;
            submit.signalSemaphoreCount = signal ? 1u : 0u;
            submit.pSignalSemaphores = &signal;
            // 先保留追踪记录，避免 GPU 提交成功后容器扩容异常丢失 fence 所有权。
            submissions.emplace_back();
            auto result = c->f.vkQueueSubmit(c->queue, 1, &submit, s.fence);
            if (result != VK_SUCCESS) {
                submissions.pop_back();
                vkCheck(result, "Submit Vulkan commands");
            }
            ++submitted;
            for (auto &native : s.resources)
                native->lastUse = submitted;
            submissions.back() = std::move(s);
            return {owner, submitted};
        } catch (...) {
            release(s);
            throw;
        }
    }
    template <class F> void immediate(F action) {
        auto s = begin();
        try {
            action(s);
        } catch (...) {
            release(s);
            throw;
        }
        wait(queue(std::move(s)));
    }
    void memoryBarrier(VkCommandBuffer command,
                       VkPipelineStageFlags2 source = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                       VkAccessFlags2 before = VK_ACCESS_2_MEMORY_READ_BIT |
                                               VK_ACCESS_2_MEMORY_WRITE_BIT,
                       VkPipelineStageFlags2 destination = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                       VkAccessFlags2 after = VK_ACCESS_2_MEMORY_READ_BIT |
                                              VK_ACCESS_2_MEMORY_WRITE_BIT) {
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = source;
        barrier.srcAccessMask = before;
        barrier.dstStageMask = destination;
        barrier.dstAccessMask = after;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;
        c->f.vkCmdPipelineBarrier2(command, &dependency);
    }
    std::shared_ptr<Native> makeBuffer(size_t bytes, VkMemoryPropertyFlags properties) {
        auto native = std::make_shared<Native>(Native::Buffer, c);
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buffer.size = bytes;
        buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        vkCheck(c->f.vkCreateBuffer(c->device, &buffer, nullptr, &native->buffer),
                "Create Vulkan buffer");
        VkMemoryRequirements requirements;
        c->f.vkGetBufferMemoryRequirements(c->device, native->buffer, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = c->memoryType(requirements.memoryTypeBits, properties);
        vkCheck(c->f.vkAllocateMemory(c->device, &allocation, nullptr, &native->memory),
                "Allocate Vulkan buffer memory");
        vkCheck(c->f.vkBindBufferMemory(c->device, native->buffer, native->memory, 0),
                "Bind Vulkan buffer memory");
        if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            vkCheck(
                c->f.vkMapMemory(c->device, native->memory, 0, VK_WHOLE_SIZE, 0, &native->mapping),
                "Map Vulkan memory");
        return native;
    }
    BufferHandle createBuffer(const BufferDesc &description,
                              std::span<const std::byte> initial) override {
        checkThread();
        verify(description.size > 0, "Buffer size must be positive");
        verify(description.memory == Memory::Device || description.memory == Memory::Upload ||
                   description.memory == Memory::Readback,
               "Invalid buffer memory mode");
        range(description.size, 0, initial.size());
        auto properties =
            description.memory == Memory::Device
                ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        auto handle = buffers.create(Buffer{makeBuffer(description.size, properties), description});
        try {
            if (!initial.empty())
                upload(handle, 0, initial);
        } catch (...) {
            buffers.erase(handle);
            throw;
        }
        return handle;
    }
    std::shared_ptr<Native> imageView(const std::shared_ptr<Native> &storage, Format format,
                                      uint32_t base, uint32_t count) {
        auto view = std::make_shared<Native>(Native::View, c);
        view->parent = storage;
        VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        info.image = storage->image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = vkFormat(format);
        info.subresourceRange = {VkImageAspectFlags(format == Format::Depth32Float
                                                        ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                        : VK_IMAGE_ASPECT_COLOR_BIT),
                                 base, count, 0, 1};
        vkCheck(c->f.vkCreateImageView(c->device, &info, nullptr, &view->view),
                "Create Vulkan image view");
        return view;
    }
    TextureHandle createTexture(const TextureDesc &description) override {
        checkThread();
        verify(description.width && description.height &&
                   description.width <= caps.maxTextureSize &&
                   description.height <= caps.maxTextureSize,
               "Invalid texture dimensions");
        verify(description.mipLevels &&
                   description.mipLevels <=
                       uint32_t(std::bit_width(std::max(description.width, description.height))),
               "Invalid texture mip count");
        auto features = formatFeatures(description.format);
        verify(features.sampled, "Texture format is not supported");
        auto storage = std::make_shared<Native>(Native::Storage, c);
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = vkFormat(description.format);
        info.extent = {description.width, description.height, 1};
        info.mipLevels = description.mipLevels;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (features.colorAttachment)
            info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (features.depthAttachment)
            info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (features.storageImage)
            info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
        vkCheck(c->f.vkCreateImage(c->device, &info, nullptr, &storage->image),
                "Create Vulkan image");
        VkMemoryRequirements requirements;
        c->f.vkGetImageMemoryRequirements(c->device, storage->image, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex =
            c->memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkCheck(c->f.vkAllocateMemory(c->device, &allocation, nullptr, &storage->memory),
                "Allocate Vulkan image memory");
        vkCheck(c->f.vkBindImageMemory(c->device, storage->image, storage->memory, 0),
                "Bind Vulkan image memory");
        // RHI 图片统一使用 GENERAL，避免视图与缓存绑定组隐藏的布局状态；范围屏障负责可见性。
        immediate([&](Submission &s) {
            use(s, storage);
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = storage->image;
            barrier.subresourceRange = {
                VkImageAspectFlags(description.format == Format::Depth32Float
                                       ? VK_IMAGE_ASPECT_DEPTH_BIT
                                       : VK_IMAGE_ASPECT_COLOR_BIT),
                0, description.mipLevels, 0, 1};
            VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier;
            c->f.vkCmdPipelineBarrier2(s.command, &dependency);
        });
        return textures.create(Texture{
            imageView(storage, description.format, 0, description.mipLevels), description, 0});
    }
    TextureHandle createTextureView(TextureHandle source, uint32_t firstMip,
                                    uint32_t mipCount) override {
        checkThread();
        const auto &texture = textures.get(source);
        verify(mipCount && firstMip < texture.desc.mipLevels &&
                   mipCount <= texture.desc.mipLevels - firstMip,
               "Invalid texture view mip range");
        auto description = texture.desc;
        description.width = std::max(1u, description.width >> firstMip);
        description.height = std::max(1u, description.height >> firstMip);
        description.mipLevels = mipCount;
        const auto base = texture.baseMip + firstMip;
        return textures.create(
            Texture{imageView(texture.native->parent, description.format, base, mipCount),
                    description, base});
    }
    SamplerHandle createSampler(const SamplerDesc &description) override {
        checkThread();
        auto native = std::make_shared<Native>(Native::Sampler, c);
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        verify(
            (description.minFilter == Filter::Nearest || description.minFilter == Filter::Linear) &&
                (description.magFilter == Filter::Nearest ||
                 description.magFilter == Filter::Linear) &&
                (description.mipFilter == Filter::Nearest ||
                 description.mipFilter == Filter::Linear),
            "Invalid sampler filter");
        info.minFilter =
            description.minFilter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.magFilter =
            description.magFilter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.mipmapMode = description.mipFilter == Filter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                                  : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = vkAddress(description.addressU);
        info.addressModeV = vkAddress(description.addressV);
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.maxLod = description.mipmapped ? VK_LOD_CLAMP_NONE : 0;
        info.compareEnable = description.compare;
        info.compareOp = vkCompare(description.compareFunction);
        const auto &b = description.borderColor;
        if (b.x == 1 && b.y == 1 && b.z == 1 && b.w == 1)
            info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        else if (b.x == 0 && b.y == 0 && b.z == 0 && b.w == 1)
            info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        else if (b.x == 0 && b.y == 0 && b.z == 0 && b.w == 0)
            info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        else
            verify(description.addressU != AddressMode::Border &&
                       description.addressV != AddressMode::Border,
                   "Vulkan border sampling requires a standard black/white border color");
        vkCheck(c->f.vkCreateSampler(c->device, &info, nullptr, &native->sampler),
                "Create Vulkan sampler");
        return samplers.create(Sampler{std::move(native)});
    }

    std::shared_ptr<Native> singleMipView(Texture &texture, uint32_t mip) {
        verify(mip < texture.desc.mipLevels, "Invalid image view mip");
        if (texture.desc.mipLevels == 1)
            return texture.native;
        if (auto found = texture.singleMipViews.find(mip); found != texture.singleMipViews.end())
            return found->second;
        auto native =
            imageView(texture.native->parent, texture.desc.format, texture.baseMip + mip, 1);
        texture.singleMipViews.emplace(mip, native);
        return native;
    }

    PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc &description) override;
    PipelineHandle createComputePipeline(const ShaderArtifact &shader,
                                         const std::string &name) override;
    BindingGroupHandle createBindingGroup(const BindingGroupDesc &description) override;
    QueryHandle createTimestampQueries(uint32_t count) override;
    template <class T, class Tag>
    void destroyResource(renderer::detail::Pool<T, Tag> &pool, Handle<Tag> handle) {
        checkThread();
        collect();
        auto native = pool.get(handle).native;
        if (native->lastUse > completed)
            retired.push_back(native);
        pool.erase(handle);
    }
    void destroy(BufferHandle h) override {
        destroyResource(buffers, h);
    }
    void destroy(TextureHandle h) override {
        destroyResource(textures, h);
    }
    void destroy(SamplerHandle h) override {
        destroyResource(samplers, h);
    }
    void destroy(PipelineHandle h) override {
        destroyResource(pipelines, h);
    }
    void destroy(QueryHandle h) override {
        destroyResource(queries, h);
    }
    void destroy(BindingGroupHandle h) override {
        checkThread();
        groups.erase(h);
    }
    void *map(BufferHandle handle) override {
        checkThread();
        auto &buffer = buffers.get(handle);
        verify(buffer.desc.memory == Memory::Upload,
               "Only Upload buffers can be persistently mapped");
        return buffer.native->mapping;
    }
    void upload(BufferHandle handle, size_t offset, std::span<const std::byte> data) override {
        checkThread();
        auto &buffer = buffers.get(handle);
        range(buffer.desc.size, offset, data.size());
        if (data.empty())
            return;
        if (buffer.native->lastUse > completed)
            wait({owner, buffer.native->lastUse});
        if (buffer.native->mapping) {
            std::memcpy(static_cast<std::byte *>(buffer.native->mapping) + offset, data.data(),
                        data.size());
            return;
        }
        auto staging = makeBuffer(data.size(), VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        std::memcpy(staging->mapping, data.data(), data.size());
        immediate([&](Submission &s) {
            use(s, staging);
            use(s, buffer.native);
            memoryBarrier(s.command);
            VkBufferCopy copy{0, offset, data.size()};
            c->f.vkCmdCopyBuffer(s.command, staging->buffer, buffer.native->buffer, 1, &copy);
            memoryBarrier(s.command);
        });
    }
    void upload(TextureHandle handle, std::span<const uint8_t> pixels, uint32_t mip) override {
        checkThread();
        auto &texture = textures.get(handle);
        verify(texture.desc.format == Format::Rgba8 || texture.desc.format == Format::Rgba8Srgb,
               "RGBA8 upload requires an RGBA8 texture");
        verify(mip < texture.desc.mipLevels, "Invalid upload mip");
        const auto w = std::max(1u, texture.desc.width >> mip),
                   h = std::max(1u, texture.desc.height >> mip);
        verify(pixels.size() == size_t(w) * h * 4, "Texture upload size mismatch");
        auto staging = makeBuffer(pixels.size(), VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        std::memcpy(staging->mapping, pixels.data(), pixels.size());
        immediate([&](Submission &s) {
            use(s, staging);
            use(s, texture.native);
            memoryBarrier(s.command);
            VkBufferImageCopy copy{};
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, texture.baseMip + mip, 0, 1};
            copy.imageExtent = {w, h, 1};
            c->f.vkCmdCopyBufferToImage(s.command, staging->buffer, texture.native->parent->image,
                                        VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
            memoryBarrier(s.command);
        });
    }
    void generateMipmaps(TextureHandle handle) override;
    std::vector<std::byte> readBuffer(BufferHandle handle, size_t bytes, size_t offset) override {
        checkThread();
        auto &buffer = buffers.get(handle);
        if (!bytes) {
            verify(offset <= buffer.desc.size, "Invalid read offset");
            bytes = buffer.desc.size - offset;
        }
        range(buffer.desc.size, offset, bytes);
        if (!bytes)
            return {};
        auto staging = makeBuffer(bytes, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        immediate([&](Submission &s) {
            use(s, staging);
            use(s, buffer.native);
            memoryBarrier(s.command);
            VkBufferCopy copy{offset, 0, bytes};
            c->f.vkCmdCopyBuffer(s.command, buffer.native->buffer, staging->buffer, 1, &copy);
            memoryBarrier(s.command, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                          VK_ACCESS_2_HOST_READ_BIT);
        });
        std::vector<std::byte> result(bytes);
        std::memcpy(result.data(), staging->mapping, bytes);
        return result;
    }
    RgbaImage readTexture(TextureHandle handle) override;
    CompletionToken submit(const std::vector<detail::Command> &commands) override;
    bool complete(CompletionToken token) override {
        checkThread();
        if (!token.owner && !token.value)
            return true;
        verify(token.owner == owner && token.value <= submitted, "Invalid Vulkan completion token");
        collect();
        return token.value <= completed;
    }
    void wait(CompletionToken token) override {
        checkThread();
        if (!token.owner && !token.value)
            return;
        verify(token.owner == owner && token.value <= submitted, "Invalid Vulkan completion token");
        if (token.value <= completed)
            return;
        for (const auto &s : submissions)
            if (s.serial == token.value) {
                vkCheck(c->f.vkWaitForFences(c->device, 1, &s.fence, VK_TRUE, UINT64_MAX),
                        "Wait Vulkan submission");
                break;
            }
        collect();
    }
    void waitIdle() override {
        checkThread();
        vkCheck(c->f.vkDeviceWaitIdle(c->device), "Wait Vulkan device");
        collect();
    }
    std::optional<uint64_t> timestampResult(QueryHandle handle, uint32_t index,
                                            bool waitForResult) override;
    size_t pendingDeletions() const override {
        checkThread();
        return retired.size();
    }
    uint32_t debugErrorCount() const override {
        return c->errors.load();
    }
    void releaseContext() override {
        checkThread();
        thread = {};
    }
    void acquireContext() override {
        if (thread != std::thread::id{} && thread != std::this_thread::get_id())
            throw std::logic_error("Vulkan device belongs to another thread");
        thread = std::this_thread::get_id();
    }
    void destroySwapchain() {
        for (auto semaphore : presentSemaphores)
            c->f.vkDestroySemaphore(c->device, semaphore, nullptr);
        presentSemaphores.clear();
        swapImages.clear();
        swapInitialized.clear();
        if (swapchain)
            c->f.vkDestroySwapchainKHR(c->device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    bool createSwapchain();
    void swap() override;
    std::shared_ptr<renderer::detail::WindowState> windowState() const override {
        return window;
    }
};

namespace {
std::shared_ptr<Native> shaderModule(const std::shared_ptr<Context> &c,
                                     const ShaderArtifact &shader, ShaderStage expected) {
    verify(shader.stage == expected && !shader.spirv.empty(),
           "Invalid shader stage or empty SPIR-V");
    auto native = std::make_shared<Native>(Native::Shader, c);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = shader.spirv.size() * sizeof(uint32_t);
    info.pCode = shader.spirv.data();
    vkCheck(c->f.vkCreateShaderModule(c->device, &info, nullptr, &native->shader),
            "Create Vulkan shader module");
    return native;
}
std::vector<ShaderBinding> pipelineLayout(const std::shared_ptr<Context> &c, Native &native,
                                          std::span<const ShaderArtifact> shaders) {
    struct Slot {
        ShaderBinding binding;
        VkShaderStageFlags stages = 0;
    };
    std::map<std::pair<uint32_t, uint32_t>, Slot> slots;
    for (const auto &shader : shaders) {
        const VkShaderStageFlags stage =
            shader.stage == ShaderStage::Vertex     ? VK_SHADER_STAGE_VERTEX_BIT
            : shader.stage == ShaderStage::Fragment ? VK_SHADER_STAGE_FRAGMENT_BIT
                                                    : VK_SHADER_STAGE_COMPUTE_BIT;
        for (const auto &binding : shader.bindings) {
            verify(binding.group < c->properties.limits.maxBoundDescriptorSets &&
                       binding.arraySize == 1,
                   "Unsupported descriptor set or descriptor array");
            auto [it, inserted] =
                slots.emplace(std::pair{binding.group, binding.binding}, Slot{binding, stage});
            if (!inserted) {
                verify(it->second.binding.kind == binding.kind &&
                           it->second.binding.byteSize == binding.byteSize,
                       "Shader descriptor declarations disagree");
                it->second.stages |= stage;
            }
        }
    }
    uint32_t count = 0;
    for (const auto &[key, slot] : slots)
        count = std::max(count, key.first + 1);
    std::vector<ShaderBinding> reflected;
    for (uint32_t group = 0; group < count; ++group) {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for (const auto &[key, slot] : slots)
            if (key.first == group) {
                bindings.push_back(
                    {key.second, descriptorType(slot.binding.kind), 1, slot.stages, nullptr});
                reflected.push_back(slot.binding);
            }
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = uint32_t(bindings.size());
        info.pBindings = bindings.data();
        VkDescriptorSetLayout layout;
        vkCheck(c->f.vkCreateDescriptorSetLayout(c->device, &info, nullptr, &layout),
                "Create Vulkan descriptor layout");
        native.sets.push_back(layout);
    }
    VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount = uint32_t(native.sets.size());
    info.pSetLayouts = native.sets.data();
    vkCheck(c->f.vkCreatePipelineLayout(c->device, &info, nullptr, &native.layout),
            "Create Vulkan pipeline layout");
    return reflected;
}
} // namespace

PipelineHandle VulkanDevice::createGraphicsPipeline(const GraphicsPipelineDesc &description) {
    checkThread();
    verify(description.colorFormats.size() <= caps.maxColorAttachments &&
               (!description.colorFormats.empty() || description.depthFormat),
           "Invalid pipeline attachments");
    verify(description.colorBlends.empty() ||
               description.colorBlends.size() == description.colorFormats.size(),
           "Blend attachment count mismatch");
    verify(std::isfinite(description.depthBiasConstant) &&
               std::isfinite(description.depthBiasSlope),
           "Invalid depth bias");
    auto native = std::make_shared<Native>(Native::Pipeline, c);
    std::array shaders{description.vertex, description.fragment};
    auto bindings = pipelineLayout(c, *native, shaders);
    auto vertex = shaderModule(c, description.vertex, ShaderStage::Vertex);
    auto fragment = shaderModule(c, description.fragment, ShaderStage::Fragment);
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    for (auto &stage : stages) {
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex->shader;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment->shader;
    std::vector<VkVertexInputBindingDescription> vertexBindings;
    std::set<uint32_t> bindingIds, locations;
    for (const auto &binding : description.vertexBindings) {
        verify(binding.binding < c->properties.limits.maxVertexInputBindings && binding.stride &&
                   binding.stride <= caps.maxVertexStride &&
                   bindingIds.insert(binding.binding).second,
               "Invalid vertex binding");
        vertexBindings.push_back(
            {binding.binding, binding.stride,
             binding.instanced ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX});
    }
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (const auto &attribute : description.attributes) {
        const auto binding =
            std::find_if(description.vertexBindings.begin(), description.vertexBindings.end(),
                         [&](const auto &b) { return b.binding == attribute.binding; });
        verify(attribute.location < caps.maxVertexAttributes &&
                   locations.insert(attribute.location).second && attribute.components >= 1 &&
                   attribute.components <= 4 && binding != description.vertexBindings.end(),
               "Invalid vertex attribute");
        verify(attribute.offset <= binding->stride &&
                   attribute.components * 4 <= binding->stride - attribute.offset &&
                   attribute.offset <= c->properties.limits.maxVertexInputAttributeOffset,
               "Vertex attribute exceeds stride");
        constexpr VkFormat formats[] = {VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT,
                                        VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT};
        attributes.push_back({attribute.location, attribute.binding,
                              formats[attribute.components - 1], attribute.offset});
    }
    VkPipelineVertexInputStateCreateInfo input{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = uint32_t(vertexBindings.size());
    input.pVertexBindingDescriptions = vertexBindings.data();
    input.vertexAttributeDescriptionCount = uint32_t(attributes.size());
    input.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1;
    verify(description.cull == CullMode::None || description.cull == CullMode::Back ||
               description.cull == CullMode::Front,
           "Invalid cull mode");
    raster.cullMode = description.cull == CullMode::None   ? VK_CULL_MODE_NONE
                      : description.cull == CullMode::Back ? VK_CULL_MODE_BACK_BIT
                                                           : VK_CULL_MODE_FRONT_BIT;
    // 保持 RHI 的左下图片行序，正高度 viewport 后用 Vulkan 屏幕绕序补偿。
    raster.frontFace =
        description.clockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    raster.depthBiasEnable = description.depthBiasConstant != 0 || description.depthBiasSlope != 0;
    raster.depthBiasConstantFactor = description.depthBiasConstant;
    raster.depthBiasSlopeFactor = description.depthBiasSlope;
    VkPipelineMultisampleStateCreateInfo samples{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = description.depthTest;
    depth.depthWriteEnable = description.depthWrite;
    depth.depthCompareOp = vkCompare(description.depthCompare);
    std::vector<VkFormat> formats;
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;
    for (size_t index = 0; index < description.colorFormats.size(); ++index) {
        verify(formatFeatures(description.colorFormats[index]).colorAttachment,
               "Unsupported color attachment format");
        formats.push_back(vkFormat(description.colorFormats[index]));
        ColorBlendState blend;
        if (!description.colorBlends.empty())
            blend = description.colorBlends[index];
        else if (description.blend != BlendMode::Opaque) {
            verify(description.blend == BlendMode::Alpha ||
                       description.blend == BlendMode::Additive,
                   "Invalid blend mode");
            blend.enabled = true;
            blend.sourceColor = BlendFactor::SourceAlpha;
            blend.destinationColor = description.blend == BlendMode::Alpha
                                         ? BlendFactor::OneMinusSourceAlpha
                                         : BlendFactor::One;
            blend.destinationAlpha = BlendFactor::OneMinusSourceAlpha;
        }
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.blendEnable = blend.enabled;
        attachment.srcColorBlendFactor = vkBlend(blend.sourceColor);
        attachment.dstColorBlendFactor = vkBlend(blend.destinationColor);
        attachment.colorBlendOp = attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = vkBlend(blend.sourceAlpha);
        attachment.dstAlphaBlendFactor = vkBlend(blend.destinationAlpha);
        attachment.colorWriteMask = 15;
        blendAttachments.push_back(attachment);
    }
    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = uint32_t(blendAttachments.size());
    blend.pAttachments = blendAttachments.data();
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = states;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = uint32_t(formats.size());
    rendering.pColorAttachmentFormats = formats.data();
    if (description.depthFormat) {
        verify(formatFeatures(*description.depthFormat).depthAttachment, "Invalid depth format");
        rendering.depthAttachmentFormat = vkFormat(*description.depthFormat);
    }
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages.data();
    info.pVertexInputState = &input;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &samples;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = native->layout;
    vkCheck(c->f.vkCreateGraphicsPipelines(c->device, c->pipelineCache, 1, &info, nullptr,
                                           &native->pipeline),
            "Create Vulkan graphics pipeline");
    return pipelines.create(Pipeline{native, description, false, std::move(bindings)});
}
PipelineHandle VulkanDevice::createComputePipeline(const ShaderArtifact &shader,
                                                   const std::string &) {
    checkThread();
    auto native = std::make_shared<Native>(Native::Pipeline, c);
    const std::array shaders{shader};
    auto bindings = pipelineLayout(c, *native, shaders);
    auto module = shaderModule(c, shader, ShaderStage::Compute);
    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module->shader;
    info.stage.pName = "main";
    info.layout = native->layout;
    vkCheck(c->f.vkCreateComputePipelines(c->device, c->pipelineCache, 1, &info, nullptr,
                                          &native->pipeline),
            "Create Vulkan compute pipeline");
    return pipelines.create(Pipeline{native, {}, true, std::move(bindings)});
}
BindingGroupHandle VulkanDevice::createBindingGroup(const BindingGroupDesc &description) {
    checkThread();
    verify(description.group < c->properties.limits.maxBoundDescriptorSets,
           "Descriptor set exceeds device limit");
    std::set<uint32_t> seen;
    for (const auto &binding : description.entries) {
        verify(seen.insert(binding.binding).second, "Duplicate descriptor binding");
        if (binding.kind == BindingKind::UniformBuffer ||
            binding.kind == BindingKind::StorageBuffer) {
            const auto &buffer = buffers.get(binding.buffer);
            verify(binding.offset < buffer.desc.size, "Descriptor offset exceeds buffer");
            const auto size = binding.size ? binding.size : buffer.desc.size - binding.offset;
            range(buffer.desc.size, binding.offset, size);
            const auto alignment = binding.kind == BindingKind::UniformBuffer
                                       ? caps.uniformAlignment
                                       : caps.storageAlignment;
            verify(binding.offset % alignment == 0, "Descriptor buffer offset is not aligned");
            const auto limit = binding.kind == BindingKind::UniformBuffer
                                   ? c->properties.limits.maxUniformBufferRange
                                   : c->properties.limits.maxStorageBufferRange;
            verify(size <= limit, "Descriptor buffer range exceeds device limit");
        } else {
            const auto &texture = textures.get(binding.texture);
            verify(binding.mipLevel < texture.desc.mipLevels, "Descriptor mip out of range");
            if (binding.kind == BindingKind::SampledTexture)
                samplers.get(binding.sampler);
            else
                verify(binding.kind == BindingKind::StorageImage &&
                           formatFeatures(texture.desc.format).storageImage,
                       "Invalid storage image descriptor");
        }
    }
    return groups.create(description);
}
QueryHandle VulkanDevice::createTimestampQueries(uint32_t count) {
    checkThread();
    verify(count > 0 && count <= 65536, "Invalid timestamp query count");
    auto native = std::make_shared<Native>(Native::Query, c);
    VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = count;
    vkCheck(c->f.vkCreateQueryPool(c->device, &info, nullptr, &native->query),
            "Create Vulkan timestamp pool");
    return queries.create(Queries{native, std::vector<bool>(count, false)});
}
std::optional<uint64_t> VulkanDevice::timestampResult(QueryHandle handle, uint32_t index,
                                                      bool waitForResult) {
    checkThread();
    auto &query = queries.get(handle);
    verify(index < query.written.size(), "Timestamp query index out of range");
    if (!query.written[index])
        return {};
    uint64_t result = 0;
    const auto status = c->f.vkGetQueryPoolResults(
        c->device, query.native->query, index, 1, sizeof(result), &result, sizeof(result),
        VK_QUERY_RESULT_64_BIT | (waitForResult ? VK_QUERY_RESULT_WAIT_BIT : 0));
    if (status == VK_NOT_READY)
        return {};
    vkCheck(status, "Read Vulkan timestamp");
    if (c->timestampBits < 64)
        result &= (uint64_t(1) << c->timestampBits) - 1;
    return static_cast<uint64_t>(static_cast<long double>(result) *
                                 c->properties.limits.timestampPeriod);
}
void VulkanDevice::generateMipmaps(TextureHandle handle) {
    checkThread();
    auto &texture = textures.get(handle);
    if (texture.desc.mipLevels == 1)
        return;
    VkFormatProperties properties;
    c->i.vkGetPhysicalDeviceFormatProperties(c->physical, vkFormat(texture.desc.format),
                                             &properties);
    verify((properties.optimalTilingFeatures &
            (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
             VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) ==
               (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT),
           "Texture format does not support linear mip generation");
    immediate([&](Submission &s) {
        use(s, texture.native);
        memoryBarrier(s.command);
        for (uint32_t mip = 1; mip < texture.desc.mipLevels; ++mip) {
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, texture.baseMip + mip - 1, 0, 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, texture.baseMip + mip, 0, 1};
            blit.srcOffsets[1] = {int32_t(std::max(1u, texture.desc.width >> (mip - 1))),
                                  int32_t(std::max(1u, texture.desc.height >> (mip - 1))), 1};
            blit.dstOffsets[1] = {int32_t(std::max(1u, texture.desc.width >> mip)),
                                  int32_t(std::max(1u, texture.desc.height >> mip)), 1};
            const auto image = texture.native->parent->image;
            c->f.vkCmdBlitImage(s.command, image, VK_IMAGE_LAYOUT_GENERAL, image,
                                VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_LINEAR);
            memoryBarrier(s.command);
        }
    });
}
RgbaImage VulkanDevice::readTexture(TextureHandle handle) {
    checkThread();
    auto &texture = textures.get(handle);
    verify(texture.desc.format != Format::Depth32Float, "Color readback requires a color texture");
    const size_t bpp = pixelBytes(texture.desc.format),
                 pixels = size_t(texture.desc.width) * texture.desc.height;
    verify(pixels <= std::numeric_limits<size_t>::max() / bpp, "Readback size overflow");
    auto staging = makeBuffer(pixels * bpp, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    immediate([&](Submission &s) {
        use(s, staging);
        use(s, texture.native);
        memoryBarrier(s.command);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, texture.baseMip, 0, 1};
        copy.imageExtent = {texture.desc.width, texture.desc.height, 1};
        c->f.vkCmdCopyImageToBuffer(s.command, texture.native->parent->image,
                                    VK_IMAGE_LAYOUT_GENERAL, staging->buffer, 1, &copy);
        memoryBarrier(s.command, VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                      VK_ACCESS_2_HOST_READ_BIT);
    });
    RgbaImage result{texture.desc.width, texture.desc.height, std::vector<uint8_t>(pixels * 4)};
    const auto *raw = static_cast<const uint8_t *>(staging->mapping);
    for (uint32_t y = 0; y < result.height; ++y)
        for (uint32_t x = 0; x < result.width; ++x) {
            const size_t source = (size_t(result.height - 1 - y) * result.width + x) * bpp;
            const size_t destination = (size_t(y) * result.width + x) * 4;
            if (texture.desc.format == Format::Rgba8 || texture.desc.format == Format::Rgba8Srgb)
                std::memcpy(result.pixels.data() + destination, raw + source, 4);
            else
                for (size_t channel = 0; channel < 4; ++channel) {
                    float value = channel == 3 ? 1.f : 0.f;
                    if (texture.desc.format == Format::Rgba16Float) {
                        uint16_t half;
                        std::memcpy(&half, raw + source + channel * 2, 2);
                        value = halfFloat(half);
                    } else if (texture.desc.format == Format::Rgba32Float || channel == 0)
                        std::memcpy(&value, raw + source + channel * 4, 4);
                    result.pixels[destination + channel] = uint8_t(std::lround(
                        std::clamp(std::isfinite(value) ? value : 0.f, 0.f, 1.f) * 255));
                }
        }
    return result;
}
CompletionToken VulkanDevice::submit(const std::vector<detail::Command> &commands) {
    checkThread();
    collect();
    auto submission = begin();
    Pipeline *pipeline = nullptr;
    bool rendering = false;
    RenderingDesc attachments;
    uint32_t width = 0, height = 0;
    std::map<uint32_t, BindingGroupHandle> activeGroups;
    std::map<uint32_t, detail::VertexBuffer> vertexBuffers;
    std::optional<detail::IndexBuffer> indexBuffer;
    std::optional<Texture> nextPresent;
    std::vector<std::pair<Queries *, uint32_t>> written;
    std::map<std::vector<uint64_t>, std::vector<VkDescriptorSet>> descriptorCache;
    auto createDescriptorPool = [&] {
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4096},
                                        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4096}};
        VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        info.maxSets = 1024;
        info.poolSizeCount = 4;
        info.pPoolSizes = sizes;
        VkDescriptorPool pool;
        vkCheck(c->f.vkCreateDescriptorPool(c->device, &info, nullptr, &pool),
                "Create Vulkan descriptor pool");
        submission.descriptors.push_back(pool);
    };
    auto descriptors = [&] {
        const auto &p = *pipeline;
        if (p.native->sets.empty())
            return;
        std::vector<uint64_t> key{uint64_t(reinterpret_cast<uintptr_t>(p.native.get()))};
        for (uint32_t set = 0; set < p.native->sets.size(); ++set) {
            auto found = activeGroups.find(set);
            if (found == activeGroups.end())
                key.insert(key.end(), {0, 0, 0});
            else
                key.insert(key.end(),
                           {found->second.owner, found->second.generation, found->second.index});
        }
        // 同一绑定组合在不同附件下仍须检查反馈，避免缓存命中绕过反馈校验。
        for (const auto &attachment : attachments.colors)
            key.push_back(uint64_t(
                reinterpret_cast<uintptr_t>(textures.get(attachment.texture).native.get())));
        if (attachments.depth)
            key.push_back(uint64_t(reinterpret_cast<uintptr_t>(
                textures.get(attachments.depth->texture).native.get())));
        auto existing = descriptorCache.find(key);
        if (existing != descriptorCache.end()) {
            c->f.vkCmdBindDescriptorSets(submission.command,
                                         p.compute ? VK_PIPELINE_BIND_POINT_COMPUTE
                                                   : VK_PIPELINE_BIND_POINT_GRAPHICS,
                                         p.native->layout, 0, uint32_t(existing->second.size()),
                                         existing->second.data(), 0, nullptr);
            return;
        }
        std::vector<VkDescriptorSet> sets(p.native->sets.size());
        if (submission.descriptors.empty())
            createDescriptorPool();
        VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocation.descriptorSetCount = uint32_t(sets.size());
        allocation.pSetLayouts = p.native->sets.data();
        allocation.descriptorPool = submission.descriptors.back();
        auto status = c->f.vkAllocateDescriptorSets(c->device, &allocation, sets.data());
        if (status == VK_ERROR_OUT_OF_POOL_MEMORY || status == VK_ERROR_FRAGMENTED_POOL) {
            createDescriptorPool();
            allocation.descriptorPool = submission.descriptors.back();
            status = c->f.vkAllocateDescriptorSets(c->device, &allocation, sets.data());
        }
        vkCheck(status, "Allocate Vulkan descriptor sets");
        std::vector<VkDescriptorBufferInfo> bufferInfo(p.bindings.size());
        std::vector<VkDescriptorImageInfo> imageInfo(p.bindings.size());
        std::vector<VkWriteDescriptorSet> writes;
        for (size_t index = 0; index < p.bindings.size(); ++index) {
            const auto &slot = p.bindings[index];
            auto group = activeGroups.find(slot.group);
            verify(group != activeGroups.end(), "Required descriptor set is missing");
            const auto &description = groups.get(group->second);
            auto entry =
                std::find_if(description.entries.begin(), description.entries.end(),
                             [&](const auto &binding) { return binding.binding == slot.binding; });
            verify(entry != description.entries.end() && entry->kind == slot.kind,
                   "Required descriptor binding is missing or has wrong type");
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = sets[slot.group];
            write.dstBinding = slot.binding;
            write.descriptorCount = 1;
            write.descriptorType = descriptorType(slot.kind);
            if (slot.kind == BindingKind::UniformBuffer ||
                slot.kind == BindingKind::StorageBuffer) {
                auto &buffer = buffers.get(entry->buffer);
                verify(entry->offset < buffer.desc.size, "Descriptor offset exceeds buffer");
                const auto size = entry->size ? entry->size : buffer.desc.size - entry->offset;
                range(buffer.desc.size, entry->offset, size);
                verify(size >= slot.byteSize, "Descriptor buffer is smaller than shader block");
                bufferInfo[index] = {buffer.native->buffer, entry->offset, size};
                write.pBufferInfo = &bufferInfo[index];
                use(submission, buffer.native);
            } else {
                auto &texture = textures.get(entry->texture);
                if (rendering) {
                    auto overlaps = [&](TextureHandle attachment, uint32_t mip) {
                        const auto &other = textures.get(attachment);
                        const auto absolute = other.baseMip + mip;
                        return texture.native->parent == other.native->parent &&
                               absolute >= texture.baseMip &&
                               absolute < texture.baseMip + texture.desc.mipLevels;
                    };
                    for (const auto &attachment : attachments.colors)
                        verify(!overlaps(attachment.texture, attachment.mipLevel),
                               "Texture attachment feedback is not supported");
                    if (attachments.depth)
                        verify(!overlaps(attachments.depth->texture, attachments.depth->mipLevel),
                               "Depth attachment feedback is not supported");
                }
                auto view = entry->kind == BindingKind::StorageImage
                                ? singleMipView(texture, entry->mipLevel)
                                : texture.native;
                imageInfo[index].imageView = view->view;
                imageInfo[index].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                use(submission, view);
                if (slot.kind == BindingKind::SampledTexture) {
                    auto &sampler = samplers.get(entry->sampler);
                    imageInfo[index].sampler = sampler.native->sampler;
                    use(submission, sampler.native);
                }
                write.pImageInfo = &imageInfo[index];
            }
            writes.push_back(write);
        }
        c->f.vkUpdateDescriptorSets(c->device, uint32_t(writes.size()), writes.data(), 0, nullptr);
        c->f.vkCmdBindDescriptorSets(
            submission.command,
            p.compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
            p.native->layout, 0, uint32_t(sets.size()), sets.data(), 0, nullptr);
        descriptorCache.emplace(std::move(key), std::move(sets));
    };
    auto prepare = [&](bool compute, uint32_t count = 0, uint32_t instances = 0,
                       uint32_t firstInstance = 0) {
        verify(pipeline && pipeline->compute == compute && rendering != compute,
               "Pipeline/pass kind is incompatible with command");
        use(submission, pipeline->native);
        descriptors();
        if (compute)
            return;
        for (const auto &layout : pipeline->desc.vertexBindings) {
            const auto binding = vertexBuffers.find(layout.binding);
            verify(binding != vertexBuffers.end(), "Required vertex buffer is missing");
            auto &buffer = buffers.get(binding->second.handle);
            const uint64_t elements =
                layout.instanced ? uint64_t(firstInstance) + instances : std::max(1u, count);
            verify(elements <= std::numeric_limits<size_t>::max() / layout.stride,
                   "Vertex range overflow");
            range(buffer.desc.size, binding->second.offset, size_t(elements * layout.stride));
            VkDeviceSize offset = binding->second.offset;
            c->f.vkCmdBindVertexBuffers(submission.command, layout.binding, 1,
                                        &buffer.native->buffer, &offset);
            use(submission, buffer.native);
        }
        if (indexBuffer) {
            auto &buffer = buffers.get(indexBuffer->handle);
            verify(indexBuffer->offset % 4 == 0 && indexBuffer->offset < buffer.desc.size,
                   "Invalid index offset");
            c->f.vkCmdBindIndexBuffer(submission.command, buffer.native->buffer,
                                      indexBuffer->offset, VK_INDEX_TYPE_UINT32);
            use(submission, buffer.native);
        }
    };
    try {
        // Host coherent 上传遵循提交前写入；显式建立主机写到 GPU 读取的可见性。
        memoryBarrier(submission.command, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT);
        std::set<std::pair<uint64_t, uint32_t>> resetQueries;
        for (const auto &command : commands)
            if (auto timestamp = std::get_if<detail::Timestamp>(&command)) {
                auto &query = queries.get(timestamp->query);
                verify(timestamp->index < query.written.size(), "Timestamp query out of range");
                verify(resetQueries.emplace(timestamp->query.index, timestamp->index).second,
                       "Timestamp index written twice in one submission");
                c->f.vkCmdResetQueryPool(submission.command, query.native->query, timestamp->index,
                                         1);
                written.emplace_back(&query, timestamp->index);
                use(submission, query.native);
            }
        for (const auto &command : commands)
            std::visit(
                [&](const auto &value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, detail::Begin>) {
                        verify(!rendering && (!value.desc.colors.empty() || value.desc.depth),
                               "Invalid rendering scope");
                        verify(value.desc.colors.size() <= caps.maxColorAttachments,
                               "Too many color attachments");
                        attachments = value.desc;
                        width = height = 0;
                        pipeline = nullptr;
                        std::vector<VkRenderingAttachmentInfo> colors;
                        auto attachment = [&](TextureHandle handle, uint32_t mip, LoadOp load,
                                              StoreOp store, bool depth) {
                            auto &texture = textures.get(handle);
                            verify((texture.desc.format == Format::Depth32Float) == depth,
                                   "Attachment format/type mismatch");
                            auto view = singleMipView(texture, mip);
                            use(submission, view);
                            const auto w = std::max(1u, texture.desc.width >> mip),
                                       h = std::max(1u, texture.desc.height >> mip);
                            verify(!width || (width == w && height == h),
                                   "Rendering attachment dimensions differ");
                            width = w;
                            height = h;
                            verify(load == LoadOp::Load || load == LoadOp::Clear ||
                                       load == LoadOp::DontCare,
                                   "Invalid load operation");
                            verify(store == StoreOp::Store || store == StoreOp::DontCare,
                                   "Invalid store operation");
                            VkRenderingAttachmentInfo result{
                                VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
                            result.imageView = view->view;
                            result.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                            result.loadOp = load == LoadOp::Load ? VK_ATTACHMENT_LOAD_OP_LOAD
                                            : load == LoadOp::Clear
                                                ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                            result.storeOp = store == StoreOp::Store
                                                 ? VK_ATTACHMENT_STORE_OP_STORE
                                                 : VK_ATTACHMENT_STORE_OP_DONT_CARE;
                            return result;
                        };
                        for (const auto &color : attachments.colors) {
                            auto info = attachment(color.texture, color.mipLevel, color.load,
                                                   color.store, false);
                            info.clearValue.color = {{color.clearColor.x, color.clearColor.y,
                                                      color.clearColor.z, color.clearColor.w}};
                            colors.push_back(info);
                        }
                        VkRenderingAttachmentInfo depth{};
                        if (attachments.depth) {
                            const auto &d = *attachments.depth;
                            verify(std::isfinite(d.clearDepth) && d.clearDepth >= 0 &&
                                       d.clearDepth <= 1,
                                   "Invalid clear depth");
                            depth = attachment(d.texture, d.mipLevel, d.load, d.store, true);
                            depth.clearValue.depthStencil.depth = d.clearDepth;
                        }
                        memoryBarrier(submission.command);
                        VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
                        info.renderArea.extent = {width, height};
                        info.layerCount = 1;
                        info.colorAttachmentCount = uint32_t(colors.size());
                        info.pColorAttachments = colors.data();
                        info.pDepthAttachment = attachments.depth ? &depth : nullptr;
                        c->f.vkCmdBeginRendering(submission.command, &info);
                        rendering = true;
                        VkViewport viewport{0, 0, float(width), float(height), 0, 1};
                        c->f.vkCmdSetViewport(submission.command, 0, 1, &viewport);
                        VkRect2D scissor{{0, 0}, {width, height}};
                        c->f.vkCmdSetScissor(submission.command, 0, 1, &scissor);
                    } else if constexpr (std::is_same_v<T, detail::End>) {
                        verify(rendering, "No rendering scope to end");
                        c->f.vkCmdEndRendering(submission.command);
                        rendering = false;
                    } else if constexpr (std::is_same_v<T, detail::Pipeline>) {
                        pipeline = &pipelines.get(value.handle);
                        if (!pipeline->compute) {
                            const auto &d = pipeline->desc;
                            verify(rendering &&
                                       d.colorFormats.size() == attachments.colors.size() &&
                                       d.depthFormat.has_value() == attachments.depth.has_value(),
                                   "Pipeline attachment layout mismatch");
                            for (size_t index = 0; index < d.colorFormats.size(); ++index)
                                verify(
                                    d.colorFormats[index] ==
                                        textures.get(attachments.colors[index].texture).desc.format,
                                    "Pipeline color format mismatch");
                            if (d.depthFormat)
                                verify(*d.depthFormat ==
                                           textures.get(attachments.depth->texture).desc.format,
                                       "Pipeline depth format mismatch");
                        } else
                            verify(!rendering, "Compute pipeline cannot be bound inside rendering");
                        c->f.vkCmdBindPipeline(submission.command,
                                               pipeline->compute ? VK_PIPELINE_BIND_POINT_COMPUTE
                                                                 : VK_PIPELINE_BIND_POINT_GRAPHICS,
                                               pipeline->native->pipeline);
                        use(submission, pipeline->native);
                    } else if constexpr (std::is_same_v<T, detail::Group>) {
                        const auto &group = groups.get(value.handle);
                        activeGroups[group.group] = value.handle;
                    } else if constexpr (std::is_same_v<T, detail::VertexBuffer>) {
                        buffers.get(value.handle);
                        vertexBuffers[value.binding] = value;
                    } else if constexpr (std::is_same_v<T, detail::IndexBuffer>) {
                        buffers.get(value.handle);
                        indexBuffer = value;
                    } else if constexpr (std::is_same_v<T, detail::Viewport>) {
                        verify(rendering && value.width && value.height &&
                                   value.width <= c->properties.limits.maxViewportDimensions[0] &&
                                   value.height <= c->properties.limits.maxViewportDimensions[1],
                               "Invalid viewport");
                        VkViewport viewport{0, 0, float(value.width), float(value.height), 0, 1};
                        c->f.vkCmdSetViewport(submission.command, 0, 1, &viewport);
                    } else if constexpr (std::is_same_v<T, detail::Scissor>) {
                        verify(rendering, "Scissor requires rendering");
                        const auto x = int32_t(std::clamp<int64_t>(value.x, 0, width)),
                                   y = int32_t(std::clamp<int64_t>(value.y, 0, height));
                        const auto right = std::clamp<int64_t>(int64_t(value.x) + value.width, x,
                                                               width),
                                   top = std::clamp<int64_t>(int64_t(value.y) + value.height, y,
                                                             height);
                        VkRect2D scissor{{x, y}, {uint32_t(right - x), uint32_t(top - y)}};
                        c->f.vkCmdSetScissor(submission.command, 0, 1, &scissor);
                    } else if constexpr (std::is_same_v<T, detail::Draw>) {
                        verify(uint64_t(value.first) + value.count <= UINT32_MAX,
                               "Vertex range overflow");
                        prepare(false, value.first + value.count, value.instances,
                                value.firstInstance);
                        c->f.vkCmdDraw(submission.command, value.count, value.instances,
                                       value.first, value.firstInstance);
                    } else if constexpr (std::is_same_v<T, detail::DrawIndexed>) {
                        verify(indexBuffer.has_value(), "Index buffer is missing");
                        const auto &buffer = buffers.get(indexBuffer->handle);
                        range(buffer.desc.size, indexBuffer->offset + size_t(value.first) * 4,
                              size_t(value.count) * 4);
                        prepare(false, 0, value.instances, value.firstInstance);
                        c->f.vkCmdDrawIndexed(submission.command, value.count, value.instances,
                                              value.first, value.base, value.firstInstance);
                    } else if constexpr (std::is_same_v<T, detail::Indirect>) {
                        auto &buffer = buffers.get(value.buffer);
                        verify(value.offset % 4 == 0, "Indirect offset must be aligned");
                        const size_t bytes = value.dispatch ? 12
                                             : value.count
                                                 ? size_t(value.count - 1) * value.stride + 20
                                                 : 0;
                        range(buffer.desc.size, value.offset, bytes);
                        use(submission, buffer.native);
                        prepare(value.dispatch);
                        if (value.dispatch)
                            c->f.vkCmdDispatchIndirect(submission.command, buffer.native->buffer,
                                                       value.offset);
                        else {
                            verify(indexBuffer && indexBuffer->offset == 0,
                                   "Indirect drawing requires an index buffer at offset zero");
                            verify(value.stride % 4 == 0 &&
                                       (value.count <= 1 || value.stride >= 20),
                                   "Invalid indirect stride");
                            if (c->features.multiDrawIndirect &&
                                value.count <= c->properties.limits.maxDrawIndirectCount)
                                c->f.vkCmdDrawIndexedIndirect(submission.command,
                                                              buffer.native->buffer, value.offset,
                                                              value.count, value.stride);
                            else
                                for (uint32_t index = 0; index < value.count; ++index)
                                    c->f.vkCmdDrawIndexedIndirect(
                                        submission.command, buffer.native->buffer,
                                        value.offset + VkDeviceSize(index) * value.stride, 1,
                                        value.stride);
                        }
                    } else if constexpr (std::is_same_v<T, detail::Dispatch>) {
                        prepare(true);
                        verify(value.x <= c->properties.limits.maxComputeWorkGroupCount[0] &&
                                   value.y <= c->properties.limits.maxComputeWorkGroupCount[1] &&
                                   value.z <= c->properties.limits.maxComputeWorkGroupCount[2],
                               "Compute group count exceeds device limit");
                        c->f.vkCmdDispatch(submission.command, value.x, value.y, value.z);
                    } else if constexpr (std::is_same_v<T, detail::Copy>) {
                        verify(!rendering, "Buffer copy cannot run inside rendering");
                        auto &source = buffers.get(value.source);
                        auto &destination = buffers.get(value.destination);
                        range(source.desc.size, value.sourceOffset, value.size);
                        range(destination.desc.size, value.destinationOffset, value.size);
                        verify(source.native != destination.native ||
                                   value.sourceOffset + value.size <= value.destinationOffset ||
                                   value.destinationOffset + value.size <= value.sourceOffset,
                               "Overlapping buffer copy");
                        use(submission, source.native);
                        use(submission, destination.native);
                        if (value.size) {
                            VkBufferCopy copy{value.sourceOffset, value.destinationOffset,
                                              value.size};
                            c->f.vkCmdCopyBuffer(submission.command, source.native->buffer,
                                                 destination.native->buffer, 1, &copy);
                        }
                    } else if constexpr (std::is_same_v<T, detail::CopyTexture>) {
                        verify(!rendering, "Image copy cannot run inside rendering");
                        auto &source = textures.get(value.source);
                        auto &destination = textures.get(value.destination);
                        const auto &r = value.region;
                        verify(source.desc.format == destination.desc.format &&
                                   r.sourceMip < source.desc.mipLevels &&
                                   r.destinationMip < destination.desc.mipLevels,
                               "Texture copy formats or mip levels differ");
                        const auto sw = std::max(1u, source.desc.width >> r.sourceMip),
                                   sh = std::max(1u, source.desc.height >> r.sourceMip),
                                   dw = std::max(1u, destination.desc.width >> r.destinationMip),
                                   dh = std::max(1u, destination.desc.height >> r.destinationMip);
                        range(sw, r.sourceX, r.width);
                        range(sh, r.sourceY, r.height);
                        range(dw, r.destinationX, r.width);
                        range(dh, r.destinationY, r.height);
                        verify(source.native->parent != destination.native->parent ||
                                   source.baseMip + r.sourceMip !=
                                       destination.baseMip + r.destinationMip ||
                                   r.sourceX + r.width <= r.destinationX ||
                                   r.destinationX + r.width <= r.sourceX ||
                                   r.sourceY + r.height <= r.destinationY ||
                                   r.destinationY + r.height <= r.sourceY,
                               "Overlapping texture copy");
                        VkImageCopy copy{};
                        const VkImageAspectFlags aspect = source.desc.format == Format::Depth32Float
                                                              ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                              : VK_IMAGE_ASPECT_COLOR_BIT;
                        copy.srcSubresource = {aspect, source.baseMip + r.sourceMip, 0, 1};
                        copy.dstSubresource = {aspect, destination.baseMip + r.destinationMip, 0,
                                               1};
                        copy.srcOffset = {int32_t(r.sourceX), int32_t(r.sourceY), 0};
                        copy.dstOffset = {int32_t(r.destinationX), int32_t(r.destinationY), 0};
                        copy.extent = {r.width, r.height, 1};
                        use(submission, source.native);
                        use(submission, destination.native);
                        if (r.width && r.height)
                            c->f.vkCmdCopyImage(submission.command, source.native->parent->image,
                                                VK_IMAGE_LAYOUT_GENERAL,
                                                destination.native->parent->image,
                                                VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
                    } else if constexpr (std::is_same_v<T, detail::Barriers>) {
                        verify(!rendering, "Resource barriers must be outside rendering scopes");
                        std::vector<VkBufferMemoryBarrier2> bufferBarriers;
                        std::vector<VkImageMemoryBarrier2> imageBarriers;
                        std::vector<VkMemoryBarrier2> memoryBarriers;
                        for (const auto &barrier : value.barriers) {
                            const auto source = stages(barrier.sourceStages),
                                       destination = stages(barrier.destinationStages);
                            if (barrier.buffer) {
                                verify(!barrier.texture && !barrier.image,
                                       "Invalid buffer barrier resource");
                                auto &buffer = buffers.get(barrier.buffer);
                                verify(barrier.offset <= buffer.desc.size,
                                       "Invalid barrier offset");
                                const auto bytes =
                                    barrier.size ? barrier.size : buffer.desc.size - barrier.offset;
                                range(buffer.desc.size, barrier.offset, bytes);
                                VkBufferMemoryBarrier2 b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
                                b.srcStageMask = source;
                                b.srcAccessMask = accessMask(barrier.before);
                                b.dstStageMask = destination;
                                b.dstAccessMask = accessMask(barrier.after);
                                b.srcQueueFamilyIndex = b.dstQueueFamilyIndex =
                                    VK_QUEUE_FAMILY_IGNORED;
                                b.buffer = buffer.native->buffer;
                                b.offset = barrier.offset;
                                b.size = bytes;
                                if (bytes)
                                    bufferBarriers.push_back(b);
                                use(submission, buffer.native);
                            } else if (barrier.image) {
                                verify(barrier.texture, "Image barrier must be marked as texture");
                                auto &texture = textures.get(barrier.image);
                                verify(barrier.firstMip < texture.desc.mipLevels,
                                       "Invalid barrier mip");
                                const auto count = barrier.mipCount
                                                       ? barrier.mipCount
                                                       : texture.desc.mipLevels - barrier.firstMip;
                                verify(count <= texture.desc.mipLevels - barrier.firstMip,
                                       "Invalid barrier mip count");
                                VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
                                b.srcStageMask = source;
                                b.srcAccessMask = accessMask(barrier.before);
                                b.dstStageMask = destination;
                                b.dstAccessMask = accessMask(barrier.after);
                                b.oldLayout = b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                                b.srcQueueFamilyIndex = b.dstQueueFamilyIndex =
                                    VK_QUEUE_FAMILY_IGNORED;
                                b.image = texture.native->parent->image;
                                b.subresourceRange = {
                                    VkImageAspectFlags(texture.desc.format == Format::Depth32Float
                                                           ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                           : VK_IMAGE_ASPECT_COLOR_BIT),
                                    texture.baseMip + barrier.firstMip, count, 0, 1};
                                imageBarriers.push_back(b);
                                use(submission, texture.native);
                            } else {
                                VkMemoryBarrier2 b{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
                                b.srcStageMask = source;
                                b.srcAccessMask = accessMask(barrier.before);
                                b.dstStageMask = destination;
                                b.dstAccessMask = accessMask(barrier.after);
                                memoryBarriers.push_back(b);
                            }
                        }
                        VkDependencyInfo info{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
                        info.bufferMemoryBarrierCount = uint32_t(bufferBarriers.size());
                        info.pBufferMemoryBarriers = bufferBarriers.data();
                        info.imageMemoryBarrierCount = uint32_t(imageBarriers.size());
                        info.pImageMemoryBarriers = imageBarriers.data();
                        info.memoryBarrierCount = uint32_t(memoryBarriers.size());
                        info.pMemoryBarriers = memoryBarriers.data();
                        c->f.vkCmdPipelineBarrier2(submission.command, &info);
                    } else if constexpr (std::is_same_v<T, detail::Timestamp>) {
                        auto &query = queries.get(value.query);
                        c->f.vkCmdWriteTimestamp2(submission.command,
                                                  VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                  query.native->query, value.index);
                    } else if constexpr (std::is_same_v<T, detail::Present>) {
                        verify(!rendering, "Present cannot run inside rendering");
                        auto &texture = textures.get(value.texture);
                        verify(texture.desc.format != Format::Depth32Float,
                               "Presentation requires a color texture");
                        nextPresent = texture;
                        use(submission, texture.native);
                    }
                },
                command);
        verify(!rendering, "Rendering scope was not ended");
    } catch (...) {
        release(submission);
        throw;
    }
    const auto token = queue(std::move(submission));
    for (auto [query, index] : written)
        query->written[index] = true;
    if (nextPresent)
        presentSource = std::move(nextPresent);
    return token;
}

bool VulkanDevice::createSwapchain() {
    if (window->framebufferWidth.load() <= 0 || window->framebufferHeight.load() <= 0)
        return false;
    waitIdle();
    destroySwapchain();
    VkSurfaceCapabilitiesKHR capabilities;
    vkCheck(c->i.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c->physical, c->surface, &capabilities),
            "Query Vulkan surface");
    if (capabilities.currentExtent.width != UINT32_MAX)
        extent = capabilities.currentExtent;
    else
        extent = {std::clamp(uint32_t(window->framebufferWidth.load()),
                             capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
                  std::clamp(uint32_t(window->framebufferHeight.load()),
                             capabilities.minImageExtent.height,
                             capabilities.maxImageExtent.height)};
    if (!extent.width || !extent.height)
        return false;
    verify(bool(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT),
           "Surface does not support transfer presentation");
    uint32_t count = 0;
    vkCheck(c->i.vkGetPhysicalDeviceSurfaceFormatsKHR(c->physical, c->surface, &count, nullptr),
            "Query surface formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkCheck(
        c->i.vkGetPhysicalDeviceSurfaceFormatsKHR(c->physical, c->surface, &count, formats.data()),
        "Query surface formats");
    auto format = std::find_if(formats.begin(), formats.end(), [](const auto &f) {
        return f.format == VK_FORMAT_B8G8R8A8_SRGB &&
               f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (format == formats.end())
        format = std::find_if(formats.begin(), formats.end(), [](const auto &f) {
            return f.format == VK_FORMAT_R8G8B8A8_SRGB &&
                   f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
    verify(format != formats.end(), "Surface must expose an RGBA8/BGRA8 sRGB swapchain format");
    swapFormat = format->format;
    VkPresentModeKHR present = VK_PRESENT_MODE_FIFO_KHR;
    if (!window->vsync) {
        c->i.vkGetPhysicalDeviceSurfacePresentModesKHR(c->physical, c->surface, &count, nullptr);
        std::vector<VkPresentModeKHR> modes(count);
        c->i.vkGetPhysicalDeviceSurfacePresentModesKHR(c->physical, c->surface, &count,
                                                       modes.data());
        if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end())
            present = VK_PRESENT_MODE_MAILBOX_KHR;
        else if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) !=
                 modes.end())
            present = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = c->surface;
    info.minImageCount = std::max(2u, capabilities.minImageCount + 1);
    if (capabilities.maxImageCount)
        info.minImageCount = std::min(info.minImageCount, capabilities.maxImageCount);
    info.imageFormat = format->format;
    info.imageColorSpace = format->colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    uint32_t families[] = {c->family, c->presentFamily};
    if (c->family != c->presentFamily) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = families;
    }
    info.preTransform = capabilities.currentTransform;
    info.presentMode = present;
    info.clipped = VK_TRUE;
    const auto composite = capabilities.supportedCompositeAlpha;
    info.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(composite & (~composite + 1));
    vkCheck(c->f.vkCreateSwapchainKHR(c->device, &info, nullptr, &swapchain),
            "Create Vulkan swapchain");
    vkCheck(c->f.vkGetSwapchainImagesKHR(c->device, swapchain, &count, nullptr),
            "Get swapchain images");
    swapImages.resize(count);
    vkCheck(c->f.vkGetSwapchainImagesKHR(c->device, swapchain, &count, swapImages.data()),
            "Get swapchain images");
    swapInitialized.assign(count, false);
    presentSemaphores.resize(count, VK_NULL_HANDLE);
    for (auto &semaphore : presentSemaphores) {
        VkSemaphoreCreateInfo s{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCheck(c->f.vkCreateSemaphore(c->device, &s, nullptr, &semaphore),
                "Create presentation semaphore");
    }
    return true;
}
void VulkanDevice::swap() {
    checkThread();
    collect();
    if (!presentSource || window->framebufferWidth.load() <= 0 ||
        window->framebufferHeight.load() <= 0)
        return;
    if (!swapchain || extent.width != uint32_t(window->framebufferWidth.load()) ||
        extent.height != uint32_t(window->framebufferHeight.load()))
        if (!createSwapchain())
            return;
    if (presentSource->desc.format != Format::Rgba8Srgb &&
        (presentWidth != presentSource->desc.width ||
         presentHeight != presentSource->desc.height)) {
        if (encodedPresent)
            destroy(encodedPresent);
        if (linearPresent)
            destroy(linearPresent);
        encodedPresent = createTexture({presentSource->desc.width, presentSource->desc.height, 1,
                                        Format::Rgba8Srgb, "Presentation encoded bits"});
        linearPresent = createTexture({presentSource->desc.width, presentSource->desc.height, 1,
                                       Format::Rgba8, "Presentation linear conversion"});
        presentWidth = presentSource->desc.width;
        presentHeight = presentSource->desc.height;
    }
    VkSemaphore available = VK_NULL_HANDLE;
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    vkCheck(c->f.vkCreateSemaphore(c->device, &semaphore, nullptr, &available),
            "Create acquire semaphore");
    uint32_t index = 0;
    auto status = c->f.vkAcquireNextImageKHR(c->device, swapchain, UINT64_MAX, available,
                                             VK_NULL_HANDLE, &index);
    if (status == VK_ERROR_OUT_OF_DATE_KHR) {
        c->f.vkDestroySemaphore(c->device, available, nullptr);
        createSwapchain();
        return;
    }
    if (status != VK_SUCCESS && status != VK_SUBOPTIMAL_KHR) {
        c->f.vkDestroySemaphore(c->device, available, nullptr);
        vkCheck(status, "Acquire swapchain image");
    }
    auto submission = begin();
    submission.acquired = available;
    try {
        use(submission, presentSource->native);
        memoryBarrier(submission.command);
        auto sourceImage = presentSource->native->parent->image;
        auto sourceMip = presentSource->baseMip;
        if (presentSource->desc.format != Format::Rgba8Srgb) {
            auto &encoded = textures.get(encodedPresent);
            use(submission, encoded.native);
            if (presentSource->desc.format != Format::Rgba8) {
                auto &linear = textures.get(linearPresent);
                use(submission, linear.native);
                VkImageBlit conversion{};
                conversion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, sourceMip, 0, 1};
                conversion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                conversion.srcOffsets[1] = conversion.dstOffsets[1] = {
                    int32_t(presentSource->desc.width), int32_t(presentSource->desc.height), 1};
                c->f.vkCmdBlitImage(submission.command, sourceImage, VK_IMAGE_LAYOUT_GENERAL,
                                    linear.native->parent->image, VK_IMAGE_LAYOUT_GENERAL, 1,
                                    &conversion, VK_FILTER_NEAREST);
                memoryBarrier(submission.command);
                sourceImage = linear.native->parent->image;
                sourceMip = 0;
            }
            // 将线性格式的呈现字节按位解释为显示编码，匹配 OpenGL 默认帧缓冲拷贝语义。
            VkImageCopy copy{};
            copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, sourceMip, 0, 1};
            copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.extent = {presentSource->desc.width, presentSource->desc.height, 1};
            c->f.vkCmdCopyImage(submission.command, sourceImage, VK_IMAGE_LAYOUT_GENERAL,
                                encoded.native->parent->image, VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
            memoryBarrier(submission.command);
            sourceImage = encoded.native->parent->image;
            sourceMip = 0;
        }
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.oldLayout =
            swapInitialized[index] ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = swapImages[index];
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &barrier;
        c->f.vkCmdPipelineBarrier2(submission.command, &dependency);
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, sourceMip, 0, 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        // RHI 图片存储保留左下行序；窗口呈现时翻转，和 readback 的左上行序一致。
        blit.srcOffsets[0] = {0, int32_t(presentSource->desc.height), 0};
        blit.srcOffsets[1] = {int32_t(presentSource->desc.width), 0, 1};
        blit.dstOffsets[1] = {int32_t(extent.width), int32_t(extent.height), 1};
        c->f.vkCmdBlitImage(submission.command, sourceImage, VK_IMAGE_LAYOUT_GENERAL,
                            swapImages[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                            VK_FILTER_NEAREST);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = 0;
        c->f.vkCmdPipelineBarrier2(submission.command, &dependency);
    } catch (...) {
        release(submission);
        throw;
    }
    queue(std::move(submission), available, presentSemaphores[index]);
    swapInitialized[index] = true;
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &presentSemaphores[index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &index;
    status = c->f.vkQueuePresentKHR(c->presentQueue, &present);
    if (status == VK_ERROR_OUT_OF_DATE_KHR || status == VK_SUBOPTIMAL_KHR) {
        createSwapchain();
        return;
    }
    vkCheck(status, "Present Vulkan image");
}
Device Device::createVulkan(Window &window, bool debug) {
    auto impl = std::make_unique<Impl>();
    auto state = renderer::detail::WindowAccess::state(window);
    auto backend = std::make_shared<VulkanDevice>(state, debug);
    state->presentCallback = [weak = std::weak_ptr<VulkanDevice>(backend)] {
        auto device = weak.lock();
        if (!device)
            throw std::logic_error("Vulkan device was destroyed");
        device->swap();
    };
    impl->backend = std::move(backend);
    return Device(std::move(impl));
}
} // namespace renderer::rhi
