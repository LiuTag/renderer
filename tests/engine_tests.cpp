#include "internal/frame.hpp"
#include "renderer/model.hpp"
#include "renderer/render_graph.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace renderer;
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void throws(F &&f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "Expected validation error");
}
auto noop = [](rhi::CommandEncoder &, const GraphResources &) {};
const std::filesystem::path fixturePath = RENDERER_TEST_ASSETS;
} // namespace
int main() {
    std::vector<std::pair<const char *, std::function<void()>>> tests = {
        {"shader SPIR-V, reflection, dense OpenGL remapping and cache",
         [] {
             ShaderCompiler compiler;
             const std::string source = R"(#version 450
layout(local_size_x=1) in;
layout(std140,set=1,binding=3) uniform Params {mat4 world;vec4 color;} params;
layout(std430,set=2,binding=7) buffer Data {vec4 outputValues[];} data;
void main(){data.outputValues[gl_GlobalInvocationID.x]=params.world*params.color;}
)";
             auto shader = compiler.compile(ShaderStage::Compute, source);
             require(!shader.spirv.empty() && shader.bindings.size() == 2,
                     "SPIR-V/reflection missing");
             require(shader.bindings[0].byteSize == 80 &&
                         shader.bindings[0].members[1].offset == 64,
                     "std140 reflection offsets incorrect");
             const BindingRemap remaps[] = {{1, 3, 0}, {2, 7, 0}};
             auto glsl = compiler.toOpenGL(shader, remaps);
             require(glsl.find("#version 450") != std::string::npos &&
                         glsl.find("set =") == std::string::npos,
                     "GLSL translation failed");
             compiler.compile(ShaderStage::Compute, source);
             require(compiler.cachedShaderCount() == 1, "Compile cache missed identical shader");
             throws([&] { compiler.compile(ShaderStage::Vertex, "invalid shader"); });
         }},
        {"graph pass culling and dependency order",
         [] {
             RenderGraph graph;
             auto a = graph.createBuffer({64}), unused = graph.createBuffer({64});
             graph.addPass("producer", {{a, rhi::Access::StorageWrite}}, noop);
             graph.addPass("dead", {{unused, rhi::Access::StorageWrite}}, noop);
             graph.addPass("consumer", {{a, rhi::Access::StorageRead}}, noop, true);
             auto result = graph.compile();
             auto stats = result.statistics();
             require(stats.executedPasses == 2 && stats.culledPasses == 1, "Dead pass not culled");
             require(result.passes()[0].name == "producer" && result.passes()[1].name == "consumer",
                     "Dependency order incorrect");
             require(result.passes()[1].barriers > 0, "Storage dependency barrier missing");
         }},
        {"graph rejects uninitialized reads and cycles",
         [] {
             RenderGraph bad;
             auto r = bad.createBuffer({64});
             bad.addPass("bad", {{r, rhi::Access::UniformRead}}, noop, true);
             throws([&] { bad.compile(); });
             RenderGraph cycle;
             auto a = cycle.addPass("a", {}, noop, true), b = cycle.addPass("b", {}, noop, true);
             cycle.dependsOn(a, b);
             cycle.dependsOn(b, a);
             throws([&] { cycle.compile(); });
         }},
        {"graph ranges track partial initialization",
         [] {
             RenderGraph graph;
             auto r = graph.createBuffer({128});
             graph.addPass("first half",
                           {{r, rhi::Access::StorageWrite, rhi::Stage::Compute, {0, 0, 0, 64}}},
                           noop);
             graph.addPass("second half",
                           {{r, rhi::Access::StorageWrite, rhi::Stage::Compute, {0, 0, 64, 64}}},
                           noop);
             graph.addPass("read", {{r, rhi::Access::StorageRead}}, noop, true);
             require(graph.compile().statistics().executedPasses == 3,
                     "Initialized ranges did not combine");
             RenderGraph bad;
             auto x = bad.createTexture({16, 16, 3});
             bad.addPass("mip0", {{x, rhi::Access::ColorWrite, rhi::Stage::Attachment, {0, 1}}},
                         noop);
             bad.addPass("all", {{x, rhi::Access::SampledRead}}, noop, true);
             throws([&] { bad.compile(); });
         }},
        {"graph pools nonoverlapping compatible resources",
         [] {
             RenderGraph graph;
             auto a = graph.createBuffer({64}), b = graph.createBuffer({64});
             graph.addPass("a write", {{a, rhi::Access::StorageWrite}}, noop);
             graph.addPass("a consume", {{a, rhi::Access::StorageRead}}, noop, true);
             graph.addPass("b write", {{b, rhi::Access::StorageWrite}}, noop);
             graph.addPass("b consume", {{b, rhi::Access::StorageRead}}, noop, true);
             require(graph.compile().statistics().physicalResources == 1,
                     "Transient resources were not reused");
         }},
        {"graph detects feedback, cross-owner handles and range errors",
         [] {
             RenderGraph graph, other;
             auto a = graph.createTexture({16, 16, 1});
             throws([&] { other.exportResource(a); });
             graph.addPass("feedback",
                           {{a, rhi::Access::ColorWrite}, {a, rhi::Access::SampledRead}}, noop,
                           true);
             throws([&] { graph.compile(); });
             RenderGraph bounds;
             auto b = bounds.createBuffer({64});
             throws([&] {
                 bounds.addPass(
                     "bad", {{b, rhi::Access::StorageWrite, rhi::Stage::Compute, {0, 0, 63, 2}}},
                     noop);
             });
         }},
        {"command encoder validates pass and finish lifecycle",
         [] {
             rhi::CommandEncoder encoder;
             throws([&] { encoder.draw(3); });
             throws([&] { encoder.endRendering(); });
             auto list = encoder.finish();
             throws([&] { encoder.finish(); });
             rhi::CommandEncoder compute;
             throws([&] { compute.drawIndexedIndirect({}, 1); });
         }},
        {"glTF and GLB import preserve hierarchy, materials, cameras and reuse",
         [] {
             for (const char *filename : {"triangle.gltf", "triangle.glb"}) {
                 AssetStore store;
                 auto model = store.loadModel(fixturePath / filename);
                 require(model.nodeCount() == 3, "Model node count wrong");
                 Scene scene;
                 auto first = model.instantiate(scene, scene.root());
                 auto second = model.instantiate(scene, scene.root());
                 require(first.cameras.size() == 1 && first.nodes.size() == 3,
                         "Camera/hierarchy missing");
                 scene.setLocalTransform(second.root, Mat4::translation({3, 0, 0}));
                 RenderExtractor extractor;
                 auto snapshot = extractor.extract(scene, store);
                 require(snapshot.instanceCount() == 2 && snapshot.cameraCount() == 2,
                         "Reusable model extraction failed");
                 auto frame = detail::prepareFrame(
                     snapshot,
                     {.cameraNode = first.cameras[0], .frustumCulling = false, .shadows = false},
                     128, 128);
                 require(frame.stats.sceneDrawCalls == 1, "Imported shared geometry did not batch");
             }
         }},
        {"OBJ concave polygons and negative indices import",
         [] {
             AssetStore store;
             auto model = store.loadModel(fixturePath / "concave.obj");
             Scene scene;
             auto instance = model.instantiate(scene, scene.root());
             auto camera = scene.createNode(scene.root());
             scene.setComponent(camera, Camera{});
             scene.setLocalTransform(camera, Mat4::translation({0, 0, 4}));
             RenderExtractor extractor;
             auto frame = detail::prepareFrame(extractor.extract(scene, store),
                                               {.cameraNode = camera, .shadows = false}, 128, 128);
             require(frame.stats.triangles == 3, "Concave pentagon did not triangulate");
             require(instance.nodes.size() == 1, "OBJ instance missing");
         }},
        {"image import and malformed import validation",
         [] {
             AssetStore store;
             auto texture = store.loadTexture(fixturePath / "pixel.png");
             require(static_cast<bool>(texture), "Image import failed");
             auto atlasTexture =
                 store.loadTexture(fixturePath / "pixel.png", true,
                                   {.magFilter = TextureFilter::Nearest, .mipmapped = false});
             auto atlasMaterial = store.createMaterial({.baseColorTexture = atlasTexture});
             const auto &sampling =
                 detail::AssetAccess::material(store, atlasMaterial)->texture->description.sampling;
             require(!sampling.mipmapped && sampling.magFilter == TextureFilter::Nearest,
                     "Explicit image sampling was lost");
             throws([&] { store.loadModel(fixturePath / "invalid.gltf"); });
             throws([&] { store.loadModel(fixturePath / "unknown.fbx"); });
             throws([&] { store.loadTexture(fixturePath / "absent.png"); });
             auto model = store.loadModel(fixturePath / "triangle.gltf");
             require(model.nodeCount() > 0, "Failed import poisoned the store");
         }},
        {"transparent items preserve depth order",
         [] {
             AssetStore assets;
             auto mesh = assets.createMesh(geometry::cube());
             auto red = assets.createMaterial(
                 {.baseColor = {1, 0, 0, 0.5f}, .unlit = true, .alphaMode = AlphaMode::Blend});
             auto blue = assets.createMaterial(
                 {.baseColor = {0, 0, 1, 0.5f}, .unlit = true, .alphaMode = AlphaMode::Blend});
             Scene scene;
             auto camera = scene.createNode(scene.root());
             scene.setComponent(camera, Camera{});
             for (auto [z, m] : std::vector<std::pair<float, MaterialInstanceHandle>>{
                      {-3.0f, red}, {-6.0f, blue}, {-4.0f, red}}) {
                 auto node = scene.createNode(scene.root());
                 scene.setLocalTransform(node, Mat4::translation({0, 0, z}));
                 scene.setComponent(node, MeshRenderer{mesh, {m}});
             }
             RenderExtractor extractor;
             auto frame = detail::prepareFrame(extractor.extract(scene, assets),
                                               {.cameraNode = camera, .shadows = false}, 128, 128);
             require(frame.batches.size() == 2 &&
                         frame.batches[0].material->id != frame.batches[1].material->id,
                     "Transparency order was regrouped by material");
             require(frame.batches[0].material->baseColor.z == 1, "Far blue item must draw first");
         }},
        {"weighted OIT batches by state without changing sorted-alpha order",
         [] {
             AssetStore assets;
             auto mesh = assets.createMesh(geometry::cube());
             auto red = assets.createMaterial(
                 {.baseColor = {1, 0, 0, 0.5f}, .unlit = true, .alphaMode = AlphaMode::Blend});
             auto blue = assets.createMaterial(
                 {.baseColor = {0, 0, 1, 0.5f}, .unlit = true, .alphaMode = AlphaMode::Blend});
             Scene scene;
             auto camera = scene.createNode(scene.root());
             scene.setComponent(camera, Camera{});
             for (auto [z, material] : std::vector<std::pair<float, MaterialInstanceHandle>>{
                      {-3.0f, red}, {-4.0f, blue}, {-6.0f, red}}) {
                 auto node = scene.createNode(scene.root());
                 scene.setLocalTransform(node, Mat4::translation({0, 0, z}));
                 scene.setComponent(node, MeshRenderer{mesh, {material}});
             }
             RenderExtractor extractor;
             auto snapshot = extractor.extract(scene, assets);
             RenderView view{.cameraNode = camera, .shadows = false};
             auto sorted = detail::prepareFrame(snapshot, view, 128, 128);
             require(sorted.batches.size() == 3, "Sorted alpha lost interleaved depth order");
             view.transparency = TransparencyMode::WeightedBlendedOIT;
             auto oit = detail::prepareFrame(snapshot, view, 128, 128);
             require(oit.transparency == view.transparency && oit.batches.size() == 2 &&
                         oit.batches.front().instanceCount == 2 &&
                         oit.stats.transparentDrawCalls == 2 &&
                         oit.stats.triangles == sorted.stats.triangles,
                     "Weighted OIT did not batch compatible nonadjacent transparent instances");
             view.transparency = static_cast<TransparencyMode>(-1);
             throws([&] { detail::prepareFrame(snapshot, view, 128, 128); });
         }},
        {"anti-aliasing mode defaults, propagation and validation",
         [] {
             AssetStore assets;
             Scene scene;
             const auto camera = scene.createNode(scene.root());
             scene.setComponent(camera, Camera{});
             RenderExtractor extractor;
             const auto snapshot = extractor.extract(scene, assets);
             RenderView view{.cameraNode = camera};
             require(detail::prepareFrame(snapshot, view, 128, 128).antiAliasing ==
                         AntiAliasing::None,
                     "Anti-aliasing is not disabled by default");
             view.antiAliasing = AntiAliasing::FXAA;
             require(detail::prepareFrame(snapshot, view, 128, 128).antiAliasing ==
                         AntiAliasing::FXAA,
                     "Anti-aliasing mode did not reach prepared frame");
             view.antiAliasing = static_cast<AntiAliasing>(-1);
             throws([&] { detail::prepareFrame(snapshot, view, 128, 128); });
         }},
        {"lights and shadow casters survive camera culling",
         [] {
             AssetStore assets;
             Scene scene;
             auto camera = scene.createNode(scene.root());
             scene.setComponent(camera, Camera{});
             auto light = scene.createNode(scene.root());
             scene.setComponent(light, Light{});
             auto mesh = assets.createMesh(geometry::cube());
             auto material = assets.createMaterial({});
             auto node = scene.createNode(scene.root());
             scene.setComponent(node, MeshRenderer{mesh, {material}});
             scene.setLocalTransform(node, Mat4::translation({100, 0, -4}));
             RenderExtractor extractor;
             auto frame = detail::prepareFrame(extractor.extract(scene, assets),
                                               {.cameraNode = camera}, 128, 128);
             require(frame.stats.visibleInstances == 0 && frame.shadowBatches.size() == 1,
                     "Offscreen caster was incorrectly culled");
             require(frame.lights.size() == 1 && frame.uniforms.shadowParameters.x == 1,
                     "Light/shadow data missing");
         }},
        {"orthographic camera projection and sphere geometry", [] {
             AssetStore store;
             Scene scene;
             RenderExtractor extractor;
             auto mesh = store.createMesh(geometry::sphere(16, 8));
             auto material = store.createMaterial({});
             auto object = scene.createNode(scene.root());
             scene.setComponent(object, MeshRenderer{mesh, {material}});
             auto cam = scene.createNode(scene.root());
             scene.setLocalTransform(cam, Mat4::translation({0, 0, 4}));
             scene.setComponent(cam, Camera{.projection = Camera::Projection::Orthographic,
                                            .orthographicHeight = 4});
             auto frame = detail::prepareFrame(extractor.extract(scene, store),
                                               {.cameraNode = cam, .shadows = false}, 128, 128);
             require(frame.stats.visibleInstances == 1 &&
                         std::abs(frame.uniforms.viewProjection(0, 0) - 0.5f) < 0.0001f,
                     "Orthographic projection incorrect");
             require(frame.stats.triangles == 224, "Sphere tessellation incorrect");
         }}};
    size_t failures = 0;
    for (const auto &[name, test] : tests)
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception &e) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    std::cout << tests.size() - failures << '/' << tests.size() << " engine tests passed\n";
    return failures ? 1 : 0;
}
