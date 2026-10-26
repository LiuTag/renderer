/** @file @brief 验证 Vulkan 提交池复用、描述符溢出、VMA 子分配和在途资源/视图生命周期。 */
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <renderer/rhi.hpp>
#include <stdexcept>

using namespace renderer;
namespace {
void need(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void throws(F action) {
    bool caught = false;
    try {
        action();
    } catch (const std::exception &) {
        caught = true;
    }
    need(caught, "Invalid submission was accepted");
}
void allocations(rhi::Device &device) {
    // 先建立一块可复用暂存，后续比较排除暂存池保留的正常分配。
    device.beginUploadBatch();
    std::array<std::byte, 4096> seed{};
    auto warm = device.createBuffer({seed.size()}, seed);
    device.wait(device.endUploadBatch());
    device.destroy(warm);
    device.waitIdle();
    const auto baseline = device.statistics();
    need(baseline.available, "Vulkan allocator diagnostics unavailable");

    std::vector<rhi::BufferHandle> host, gpu;
    std::vector<rhi::TextureHandle> images;
    device.beginUploadBatch();
    for (uint32_t i = 0; i < 64; ++i) {
        std::vector<std::byte> data(4096, std::byte(i + 1));
        host.push_back(device.createBuffer({data.size(), rhi::Memory::Upload}, data));
        gpu.push_back(device.createBuffer({data.size()}, data));
    }
    for (uint32_t i = 0; i < 32; ++i) {
        auto image = device.createTexture({33, 17, 1, rhi::Format::Rgba8});
        std::vector<uint8_t> pixels(33 * 17 * 4, uint8_t(i + 20));
        device.upload(image, pixels);
        images.push_back(image);
    }
    device.wait(device.endUploadBatch());
    auto view = device.createTextureView(images.front(), 0, 1);
    device.destroy(images.front());
    images.front() = {};
    const auto live = device.statistics();
    const auto added = live.memoryAllocations - baseline.memoryAllocations;
    need(added >= 160 && live.memoryBlocks - baseline.memoryBlocks < added / 4,
         "Small resources were not suballocated into shared Vulkan memory blocks");
    need(live.memoryBlockBytes >= live.memoryAllocationBytes,
         "Allocator block/allocation accounting is inconsistent");
    for (uint32_t i : {0u, 17u, 63u}) {
        auto a = device.readBuffer(host[i]);
        auto b = device.readBuffer(gpu[i]);
        need(a == b &&
                 std::all_of(a.begin(), a.end(), [&](auto v) { return v == std::byte(i + 1); }),
             "Suballocated buffers overlapped or copied with a wrong mapping offset");
    }
    device.destroy(host[0]);
    host[0] = {};
    std::array<std::byte, 4096> changed;
    changed.fill(std::byte{199});
    device.upload(host[1], 0, changed);
    need(device.readBuffer(host[1]) == std::vector<std::byte>(changed.begin(), changed.end()),
         "Freeing a neighbour unmapped a shared host-visible block");
    auto pixels = device.readTexture(view);
    need(std::all_of(pixels.pixels.begin(), pixels.pixels.end(), [](auto x) { return x == 20; }),
         "Image view did not keep its suballocated parent alive");
    device.destroy(view);
    for (auto h : host)
        if (h)
            device.destroy(h);
    for (auto h : gpu)
        device.destroy(h);
    for (auto h : images)
        if (h)
            device.destroy(h);
    device.waitIdle();
    need(device.statistics().memoryAllocations == baseline.memoryAllocations,
         "Completed submission cache retained freed resource allocations");
    std::cout << "VMA: " << added << " allocations in " << live.memoryBlocks - baseline.memoryBlocks
              << " additional blocks\n";
}
void submissions(rhi::Device &device) {
    ShaderCompiler compiler;
    auto shader = compiler.compile(ShaderStage::Compute, R"(#version 450
layout(local_size_x=1) in;
layout(std430,set=0,binding=0) buffer Counter { uint value; } counter;
void main(){ atomicAdd(counter.value,1); }
)");
    auto pipeline = device.createComputePipeline(shader);
    const uint32_t zero = 0;
    auto storage = device.createBuffer({4}, std::as_bytes(std::span(&zero, 1)));
    std::vector<rhi::BindingGroupHandle> groups;
    rhi::CommandEncoder encoder;
    const rhi::ResourceBarrier dependency{.before = rhi::Access::StorageReadWrite,
                                          .after = rhi::Access::StorageReadWrite,
                                          .buffer = storage};
    encoder.barrier(std::span(&dependency, 1));
    encoder.bindPipeline(pipeline);
    // 超过首个描述符池的 set 容量，确保溢出池也被保留和复用。
    for (uint32_t i = 0; i < 300; ++i) {
        auto group = device.createBindingGroup(
            {0, {{.binding = 0, .kind = BindingKind::StorageBuffer, .buffer = storage}}});
        groups.push_back(group);
        encoder.bindGroup(group);
        encoder.dispatch(1);
    }
    const auto commands = encoder.finish();
    for (int i = 0; i < 2; ++i)
        device.wait(device.submit(commands));
    const auto warmed = device.statistics();
    for (int i = 0; i < 12; ++i)
        device.wait(device.submit(commands));
    const auto steady = device.statistics();
    need(steady.submissionContextsCreated == warmed.submissionContextsCreated &&
             steady.submissionContextsReused >= warmed.submissionContextsReused + 12,
         "Steady submissions kept creating command buffers/fences");
    need(steady.descriptorPoolsCreated == warmed.descriptorPoolsCreated &&
             steady.descriptorPoolsReused >= warmed.descriptorPoolsReused + 24,
         "Descriptor overflow pools were not reused");
    auto data = device.readBuffer(storage);
    uint32_t value;
    std::memcpy(&value, data.data(), sizeof(value));
    need(value == 300 * 14, "Reused command/descriptor state changed compute results");
    rhi::CommandEncoder invalid;
    invalid.bindPipeline({});
    throws([&] { device.submit(invalid.finish()); });
    device.wait(device.submit(commands));
    for (auto h : groups)
        device.destroy(h);
    device.destroy(pipeline);
    device.destroy(storage);
    device.waitIdle();
    std::cout << "Steady: created contexts " << steady.submissionContextsCreated << ", reused "
              << steady.submissionContextsReused << ", descriptor pools "
              << steady.descriptorPoolsCreated << '/' << steady.descriptorPoolsReused << '\n';
}
void inFlight(rhi::Device &device) {
    std::vector<rhi::BufferHandle> targets;
    rhi::CompletionToken last;
    for (uint32_t i = 0; i < 32; ++i) {
        std::vector<std::byte> data(4096, std::byte(i + 1));
        auto source = device.createBuffer({data.size(), rhi::Memory::Upload}, data);
        auto target = device.createBuffer({data.size()});
        targets.push_back(target);
        rhi::CommandEncoder commands;
        commands.copyBuffer(source, target, data.size());
        last = device.submit(commands.finish());
        device.destroy(source); // 原生子分配须保留到实际复制结束。
    }
    device.wait(last);
    need(device.complete(last), "Reused fence lost completion-token identity");
    for (uint32_t i = 0; i < targets.size(); ++i) {
        auto data = device.readBuffer(targets[i]);
        need(std::all_of(data.begin(), data.end(), [&](auto v) { return v == std::byte(i + 1); }),
             "In-flight suballocation was recycled before its GPU use completed");
        device.destroy(targets[i]);
    }
    device.waitIdle();
    auto stats = device.statistics();
    need(stats.cachedSubmissionContexts <= 8 && stats.cachedDescriptorPools <= 32 &&
             !stats.pendingSubmissions,
         "Completed submission cache exceeded its retention bound");
}
} // namespace
int main() {
    try {
        auto device = rhi::Device::createVulkanOffscreen(true);
        allocations(device);
        submissions(device);
        inFlight(device);
        need(!device.debugErrorCount(), "Vulkan validation reported errors");
        std::cout << "Vulkan submission reuse and memory suballocation passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
