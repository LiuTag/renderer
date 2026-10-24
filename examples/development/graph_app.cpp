// 二次开发手册配套：自定义图形 Pipeline、图资源声明、剔除、重复执行与计时。
#include <array>
#include <iostream>
#include <renderer/render_graph.hpp>
#include <renderer/window.hpp>
#include <stdexcept>

int main() {
    using namespace renderer;
    try {
        Window window({.width = 128, .height = 128, .visible = false, .vsync = false});
        auto device = rhi::Device::createOpenGL(window);
        ShaderCompiler compiler;
        rhi::GraphicsPipelineDesc pipelineDescription;
        pipelineDescription.vertex = compiler.compile(ShaderStage::Vertex, R"(
#version 450
layout(location = 0) in vec2 position;
void main() { gl_Position = vec4(position, 0.5, 1.0); }
)",
                                                      "triangle.vert");
        pipelineDescription.fragment = compiler.compile(ShaderStage::Fragment, R"(
#version 450
layout(location = 0) out vec4 color;
void main() { color = vec4(0.1, 0.5, 0.8, 1.0); }
)",
                                                        "triangle.frag");
        pipelineDescription.attributes = {{.location = 0, .binding = 0, .components = 2}};
        pipelineDescription.vertexBindings = {{.binding = 0, .stride = 2 * sizeof(float)}};
        pipelineDescription.colorFormats = {rhi::Format::Rgba8Srgb};
        pipelineDescription.depthTest = false;
        pipelineDescription.depthWrite = false;
        pipelineDescription.cull = rhi::CullMode::None;
        const auto pipeline = device.createGraphicsPipeline(pipelineDescription);
        const std::array<float, 6> vertices{-1, -1, 1, -1, 0, 1};
        const rhi::BufferDesc vertexDescription{.size = sizeof(vertices), .name = "Triangle"};
        const auto vertexBuffer =
            device.createBuffer(vertexDescription, std::as_bytes(std::span{vertices}));
        const auto [width, height] = window.framebufferSize();
        const rhi::TextureDesc imageDescription{.width = static_cast<uint32_t>(width),
                                                .height = static_cast<uint32_t>(height),
                                                .format = rhi::Format::Rgba8Srgb,
                                                .name = "Display"};
        const auto output = device.createTexture(imageDescription);
        {
            RenderGraph graph;
            const auto vb =
                graph.importBuffer(vertexBuffer, vertexDescription, rhi::Access::VertexRead);
            const auto intermediate = graph.createTexture(imageDescription);
            const auto display = graph.importTexture(output, imageDescription, rhi::Access::None);
            const auto unused = graph.createTexture(imageDescription);
            graph.addPass("Triangle",
                          {{vb, rhi::Access::VertexRead, rhi::Stage::Vertex},
                           {intermediate, rhi::Access::ColorWrite, rhi::Stage::Attachment}},
                          [=](rhi::CommandEncoder &encoder, const GraphResources &resources) {
                              rhi::RenderingDesc pass;
                              pass.colors.push_back({.texture = resources.texture(intermediate),
                                                     .clearColor = {0.01f, 0.01f, 0.01f, 1}});
                              encoder.beginRendering(pass);
                              encoder.bindPipeline(pipeline);
                              encoder.bindVertexBuffer(0, resources.buffer(vb));
                              encoder.viewport(static_cast<uint32_t>(width),
                                               static_cast<uint32_t>(height));
                              encoder.draw(3);
                              encoder.endRendering();
                          });
            graph.addPass("Copy output",
                          {{intermediate, rhi::Access::TransferRead, rhi::Stage::Transfer},
                           {display, rhi::Access::TransferWrite, rhi::Stage::Transfer}},
                          [=](rhi::CommandEncoder &encoder, const GraphResources &resources) {
                              encoder.copyTexture(resources.texture(intermediate),
                                                  resources.texture(display),
                                                  {.width = static_cast<uint32_t>(width),
                                                   .height = static_cast<uint32_t>(height)});
                          });
            graph.addPass(
                "Present", {{display, rhi::Access::Present, rhi::Stage::Transfer}},
                [=](rhi::CommandEncoder &encoder, const GraphResources &resources) {
                    encoder.present(resources.texture(display));
                },
                true); // 窗口输出是副作用，不能仅依赖颜色资源写入来保留此 Pass。
            graph.addPass("Unused clear",
                          {{unused, rhi::Access::ColorWrite, rhi::Stage::Attachment}},
                          [=](rhi::CommandEncoder &encoder, const GraphResources &resources) {
                              rhi::RenderingDesc pass;
                              pass.colors.push_back({.texture = resources.texture(unused)});
                              encoder.beginRendering(pass);
                              encoder.endRendering();
                          });
            graph.exportResource(display);
            auto compiled = graph.compile();
            const auto stats = compiled.statistics();
            if (stats.executedPasses != 3 || stats.culledPasses != 1)
                throw std::runtime_error("Unexpected render graph analysis");
            const auto timestamps = device.createTimestampQueries(stats.executedPasses * 2);
            for (int frame = 0; frame < 2; ++frame) {
                window.pollEvents();
                device.wait(compiled.execute(device, timestamps));
                window.present(); // 编码器 present 只拷贝图像，此处才交换窗口缓冲。
            }
            const auto passes = compiled.passes();
            for (uint32_t i = 0; i < passes.size(); ++i) {
                const auto start = device.timestampResult(timestamps, i * 2, true);
                const auto end = device.timestampResult(timestamps, i * 2 + 1, true);
                if (!start || !end || *end < *start)
                    throw std::runtime_error("Invalid GPU timestamp");
                std::cout << passes[i].name << ": " << double(*end - *start) / 1e6 << " ms\n";
            }
            const auto image = device.readTexture(output);
            const size_t center = (size_t{image.height / 2} * image.width + image.width / 2) * 4;
            if (image.pixels[center + 2] < 160)
                throw std::runtime_error("Triangle was not rendered at image center");
            device.destroy(timestamps);
        } // 编译图先销毁，释放图内临时纹理；导入资源仍由下面的应用代码管理。
        device.destroy(output);
        device.destroy(vertexBuffer);
        device.destroy(pipeline);
        device.waitIdle();
        return device.debugErrorCount() == 0 ? 0 : 2;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
