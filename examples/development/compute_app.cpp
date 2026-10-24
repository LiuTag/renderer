// 二次开发手册配套：自定义 Compute、逻辑绑定、显式屏障、完成标记与回读。
#include <array>
#include <cstring>
#include <iostream>
#include <renderer/rhi.hpp>
#include <renderer/window.hpp>
#include <stdexcept>

int main() {
    using namespace renderer;
    try {
        Window window({.width = 128, .height = 128, .visible = false, .vsync = false});
        auto device = rhi::Device::createOpenGL(window);
        if (!device.capabilities().compute)
            throw std::runtime_error("Compute is unavailable");
        ShaderCompiler compiler;
        const auto shader = compiler.compile(ShaderStage::Compute, R"(
#version 450
layout(local_size_x = 1) in;
layout(std430, set = 2, binding = 7) buffer Values { uint values[]; } data;
void main() {
    uint i = gl_GlobalInvocationID.x;
    data.values[i] = i * 3u + 7u;
}
)",
                                             "sequence.comp");
        const auto pipeline = device.createComputePipeline(shader, "Sequence");
        constexpr uint32_t count = 16;
        constexpr size_t byteCount = count * sizeof(uint32_t);
        const auto storage = device.createBuffer({.size = byteCount, .name = "Compute output"});
        const auto readback = device.createBuffer(
            {.size = byteCount, .memory = rhi::Memory::Readback, .name = "CPU readback"});
        const auto group =
            device.createBindingGroup({.group = 2,
                                       .entries = {{.binding = 7,
                                                    .kind = BindingKind::StorageBuffer,
                                                    .buffer = storage,
                                                    .size = byteCount}}});

        rhi::CommandEncoder encoder;
        encoder.bindPipeline(pipeline);
        encoder.bindGroup(group);
        encoder.dispatch(count); // 16 个工作组，每组只有一个调用，共写入 16 个元素。
        const rhi::ResourceBarrier barrier{.before = rhi::Access::StorageWrite,
                                           .after = rhi::Access::TransferRead,
                                           .buffer = storage,
                                           .sourceStages = rhi::Stage::Compute,
                                           .destinationStages = rhi::Stage::Transfer};
        encoder.barrier(std::span{&barrier, 1});
        encoder.copyBuffer(storage, readback, byteCount);
        const auto commands = encoder.finish();
        const auto completion = device.submit(commands);
        device.wait(completion); // submit 返回不代表 GPU 计算结束。

        const auto bytes = device.readBuffer(readback);
        std::array<uint32_t, count> values{};
        if (bytes.size() != sizeof(values))
            throw std::runtime_error("Unexpected readback size");
        std::memcpy(values.data(), bytes.data(), sizeof(values));
        for (uint32_t i = 0; i < count; ++i) {
            if (values[i] != i * 3 + 7)
                throw std::runtime_error("Compute result mismatch");
            std::cout << values[i] << (i + 1 == count ? '\n' : ' ');
        }
        // 先释放引用资源的组和管线，再释放缓冲；异常退出时 Device 也会清理资源。
        device.destroy(group);
        device.destroy(pipeline);
        device.destroy(readback);
        device.destroy(storage);
        device.waitIdle();
        return device.debugErrorCount() == 0 ? 0 : 2;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
