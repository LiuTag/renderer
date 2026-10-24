#include <exception>
#include <iostream>
#include <renderer/model.hpp>
#include <renderer/render_graph.hpp>
#include <renderer/renderer.hpp>
#include <renderer/shader.hpp>
#include <renderer/window.hpp>
#include <stdexcept>

// 此示例仅通过 find_package 接入已安装 SDK，不使用源码树或内部头文件。
int main() {
    try {
        using namespace renderer;
        AssetStore assets;
        Scene scene;
        const auto mesh = assets.createMesh(geometry::cube());
        const auto material = assets.createMaterial({});
        const auto object = scene.createNode(scene.root(), "Cube");
        scene.setComponent(object, MeshRenderer{mesh, {material}});
        const auto camera = scene.createNode(scene.root(), "Camera");
        scene.setComponent(camera, Camera{});
        scene.setLocalTransform(camera, *Mat4::lookAt({3, 2, 5}, {0, 0, 0}).inverse());
        RenderExtractor extractor;
        const auto snapshot = extractor.extract(scene, assets);
        if (snapshot.instanceCount() != 1 || snapshot.cameraCount() != 1)
            throw std::runtime_error("Unexpected snapshot counts");

        // 验证随 SDK 提供的 Shader 编译器及转译静态依赖可正常链接。
        ShaderCompiler compiler;
        const auto shader = compiler.compile(ShaderStage::Vertex, R"(
#version 450
void main() { gl_Position = vec4(0, 0, 0, 1); }
)");
        if (compiler.toOpenGL(shader).empty())
            throw std::runtime_error("Shader translation returned no source");
        Model emptyModel;
        RenderGraph graph;
        const auto compiled = graph.compile();
        if (emptyModel.nodeCount() || compiled.statistics().declaredPasses)
            throw std::runtime_error("Expected empty model and render graph");

#if SDK_CONSUMER_OPENGL
        Window window({.width = 64, .height = 64, .visible = false, .vsync = false});
        window.setMode(WindowMode::BorderlessFullscreen);
        if (window.mode() != WindowMode::BorderlessFullscreen)
            throw std::runtime_error("Fullscreen mode switch failed");
        window.setMode(WindowMode::Windowed);
        unsigned closeEvents = 0;
        auto subscription =
            window.subscribe<WindowCloseEvent>([&](const WindowCloseEvent &) { ++closeEvents; });
        {
            auto engine = Renderer::createOpenGL(window);
            RenderView view;
            view.cameraNode = camera;
            view.transparency = TransparencyMode::WeightedBlendedOIT;
            view.antiAliasing = AntiAliasing::FXAA;
            const auto stats = engine.render(snapshot, view);
            engine.present();
            engine.waitIdle();
            const auto [width, height] = window.framebufferSize();
            const auto image = engine.readback();
            if (stats.visibleInstances != 1 || stats.antiAliasingDrawCalls != 1 ||
                image.width != static_cast<uint32_t>(width) ||
                image.height != static_cast<uint32_t>(height) || engine.debugErrorCount())
                throw std::runtime_error("Unexpected OpenGL frame output");
        }
        // 低层设备工厂同样从已安装 OpenGL 库中解析。
        {
            auto device = rhi::Device::createOpenGL(window);
            device.waitIdle();
            if (device.debugErrorCount())
                throw std::runtime_error("Device reported OpenGL errors");
        }
        window.requestClose();
        window.pollEvents();
        if (closeEvents != 1 || !subscription.connected())
            throw std::runtime_error("Event subscription did not receive close notification");
#endif
        std::cout << "Installed renderer SDK verified\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 10;
    }
}
