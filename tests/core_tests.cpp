#include "internal/frame.hpp"
#include "renderer/renderer.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace renderer;
namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void near(float a, float b, float tolerance = 0.0001f) {
    require(std::abs(a - b) < tolerance, "Numeric comparison failed");
}
void sameMatrix(const Mat4 &a, const Mat4 &b) {
    for (size_t i = 0; i < 16; ++i)
        near(a.elements[i], b.elements[i]);
}
template <class F> void throws(F &&function) {
    bool caught = false;
    try {
        function();
    } catch (const std::exception &) {
        caught = true;
    }
    require(caught, "Expected operation to throw");
}
struct Fixture {
    AssetStore assets;
    Scene scene;
    RenderExtractor extractor;
    MeshHandle mesh = assets.createMesh(geometry::cube());
    MaterialInstanceHandle material =
        assets.createMaterial({.baseColor = {1, 0, 0, 1}, .unlit = true});
    NodeHandle camera = scene.createNode(scene.root(), "camera");
    Fixture() {
        scene.setComponent(camera, Camera{});
    }
    NodeHandle object(Vec3 position = {0, 0, -4}, MaterialInstanceHandle m = {}) {
        auto node = scene.createNode(scene.root());
        scene.setLocalTransform(node, Mat4::translation(position));
        scene.setComponent(node, MeshRenderer{mesh, {m ? m : material}});
        return node;
    }
    detail::PreparedFrame frame(const RenderSnapshot &snapshot) {
        return detail::prepareFrame(snapshot, RenderView{.cameraNode = camera}, 256, 256);
    }
};
} // namespace

int main() {
    const std::vector<std::pair<const char *, std::function<void()>>> tests = {
        {"projection uses zero-to-one depth",
         [] {
             const Mat4 p = Mat4::perspective(radians(60), 1, 0.1f, 100);
             const Vec4 n = p * Vec4{0, 0, -0.1f, 1}, f = p * Vec4{0, 0, -100, 1};
             near(n.z / n.w, 0);
             near(f.z / f.w, 1);
             throws([] { Mat4::perspective(0, 1, 1, 100); });
         }},
        {"affine inverse and look-at",
         [] {
             const Mat4 m =
                 Mat4::translation({4, 5, 6}) * Mat4::rotationY(0.8f) * Mat4::scaling({2, 3, 4});
             sameMatrix(m * (*m.inverse()), Mat4::identity());
             const Vec3 eye = Mat4::lookAt({0, 0, 3}, {0, 0, 0}).transformPoint({0, 0, 3});
             near(eye.x, 0);
             near(eye.y, 0);
             near(eye.z, 0);
             require(!Mat4::scaling({1, 0, 1}).inverse(), "Singular inverse must fail");
         }},
        {"bounds handle reflection and nonuniform scale",
         [] {
             const AABB b{{-1, -2, -3}, {1, 2, 3}};
             const auto result =
                 b.transformed(Mat4::translation({2, 3, 4}) * Mat4::scaling({-2, 3, 0.5f}));
             near(result.minimum.x, 0);
             near(result.maximum.x, 4);
             near(result.minimum.y, -3);
             near(result.maximum.z, 5.5f);
         }},
        {"hierarchical transforms invalidate descendants",
         [] {
             Scene s;
             const auto a = s.createNode(s.root()), b = s.createNode(a);
             s.setLocalTransform(a, Mat4::translation({1, 0, 0}));
             s.setLocalTransform(b, Mat4::translation({0, 2, 0}));
             near(s.worldTransform(b)(0, 3), 1);
             near(s.worldTransform(b)(1, 3), 2);
             s.setLocalTransform(a, Mat4::translation({9, 0, 0}));
             near(s.worldTransform(b)(0, 3), 9);
             s.setLocalTransform(s.root(), Mat4::translation({3, 0, 0}));
             near(s.worldTransform(b)(0, 3), 12);
         }},
        {"stale and cross-scene handles are rejected",
         [] {
             Scene s, other;
             const auto old = s.createNode(s.root());
             s.destroySubtree(old);
             const auto replacement = s.createNode(s.root());
             require(old.index == replacement.index, "Slot should be reused");
             require(!s.valid(old), "Old handle must be invalid");
             throws([&] { s.localTransform(old); });
             throws([&] { other.setVisible(replacement, false); });
             Scene moved = std::move(s);
             require(moved.valid(replacement), "Move preserves handles");
         }},
        {"reparent preserves affine world transform",
         [] {
             Scene s;
             const auto a = s.createNode(s.root()), b = s.createNode(s.root()),
                        child = s.createNode(a);
             s.setLocalTransform(a, Mat4::rotationY(0.6f) * Mat4::scaling({2, 3, 1}));
             s.setLocalTransform(b, Mat4::translation({5, 0, 0}) * Mat4::rotationZ(0.3f));
             s.setLocalTransform(child, Mat4::rotationX(0.7f));
             const auto world = s.worldTransform(child);
             s.reparent(child, b, ReparentMode::KeepWorld);
             sameMatrix(world, s.worldTransform(child));
             s.setLocalTransform(a, Mat4::scaling({0, 1, 1}));
             throws([&] { s.reparent(child, a, ReparentMode::KeepWorld); });
             require(s.parent(child) == b, "Failure must preserve hierarchy");
         }},
        {"cycles and root mutations are rejected",
         [] {
             Scene s;
             auto a = s.createNode(s.root()), b = s.createNode(a);
             throws([&] { s.reparent(a, b); });
             throws([&] { s.reparent(a, a); });
             throws([&] { s.reparent(s.root(), a); });
             throws([&] { s.destroySubtree(s.root()); });
         }},
        {"visibility inherits and components can be removed",
         [] {
             Fixture f;
             auto parent = f.scene.createNode(f.scene.root()), child = f.object();
             f.scene.reparent(child, parent);
             f.scene.setVisible(parent, false);
             require(!f.scene.effectiveVisible(child), "Child must inherit hidden state");
             require(f.extractor.extract(f.scene, f.assets).instanceCount() == 0,
                     "Hidden instances excluded");
             f.scene.setVisible(parent, true);
             require(f.extractor.extract(f.scene, f.assets).instanceCount() == 1,
                     "Visible instance missing");
             f.scene.removeComponent<MeshRenderer>(child);
             require(!f.scene.component<MeshRenderer>(child), "Component still exists");
             require(f.extractor.extract(f.scene, f.assets).instanceCount() == 0,
                     "Removed component was extracted");
         }},
        {"snapshots freeze transforms and own resources",
         [] {
             RenderSnapshot snapshot;
             NodeHandle node, camera;
             {
                 Fixture f;
                 node = f.object();
                 camera = f.camera;
                 snapshot = f.extractor.extract(f.scene, f.assets);
                 f.scene.setLocalTransform(node, Mat4::translation({9, 0, -4}));
                 near(snapshot.instanceTransform(node)->operator()(0, 3), 0);
                 f.scene.destroySubtree(node);
                 f.assets.destroyMesh(f.mesh);
                 f.assets.destroyMaterial(f.material);
             }
             require(snapshot.instanceCount() == 1,
                     "Snapshot lost resources after store destruction");
             auto frame = detail::prepareFrame(snapshot, RenderView{.cameraNode = camera}, 64, 64);
             require(frame.stats.drawCalls == 1, "Retained snapshot cannot be rendered");
         }},
        {"batching merges shared mesh and material",
         [] {
             Fixture f;
             for (int i = 0; i < 20; ++i)
                 f.object({(i % 5 - 2) * 0.1f, 0, -4});
             auto frame = f.frame(f.extractor.extract(f.scene, f.assets));
             require(frame.stats.visibleInstances == 20 && frame.stats.drawCalls == 1,
                     "Instances were not batched");
             require(frame.batches.front().instanceCount == 20, "Wrong instance range");
             require(frame.stats.triangles == 240, "Triangle count wrong");
         }},
        {"culling respects all six planes and camera layers",
         [] {
             Fixture f;
             f.object();
             for (Vec3 p : std::vector<Vec3>{{-100, 0, -4},
                                             {100, 0, -4},
                                             {0, -100, -4},
                                             {0, 100, -4},
                                             {0, 0, 3},
                                             {0, 0, -2000}})
                 f.object(p);
             auto filtered = f.object();
             f.scene.setComponent(filtered, MeshRenderer{f.mesh, {f.material}, 2});
             f.scene.setComponent(f.camera, Camera{.cullingMask = 1});
             auto snapshot = f.extractor.extract(f.scene, f.assets);
             auto frame = f.frame(snapshot);
             require(frame.stats.visibleInstances == 1 && frame.stats.culledInstances == 7,
                     "Frustum/layer filtering failed");
             auto uncull = detail::prepareFrame(
                 snapshot, RenderView{.cameraNode = f.camera, .frustumCulling = false}, 256, 256);
             require(uncull.stats.visibleInstances == 7,
                     "Layer mask must remain active with frustum culling disabled");
         }},
        {"mirrored transforms split winding batches",
         [] {
             Fixture f;
             f.object();
             auto mirrored = f.object();
             f.scene.setLocalTransform(mirrored,
                                       Mat4::translation({0, 0, -4}) * Mat4::scaling({-1, 1, 1}));
             auto frame = f.frame(f.extractor.extract(f.scene, f.assets));
             require(frame.stats.drawCalls == 2, "Reflected winding needs its own batch");
         }},
        {"submeshes select material slots",
         [] {
             Fixture f;
             auto mesh = geometry::cube();
             mesh.submeshes = {{0, 18, 0}, {18, 18, 1}};
             auto handle = f.assets.createMesh(std::move(mesh));
             auto node = f.scene.createNode(f.scene.root());
             f.scene.setLocalTransform(node, Mat4::translation({0, 0, -4}));
             f.scene.setComponent(node, MeshRenderer{handle, {f.material}});
             throws([&] { f.extractor.extract(f.scene, f.assets); });
             auto green = f.assets.createMaterial({.baseColor = {0, 1, 0, 1}});
             f.scene.setComponent(node, MeshRenderer{handle, {f.material, green}});
             auto frame = f.frame(f.extractor.extract(f.scene, f.assets));
             require(frame.stats.drawCalls == 2, "Submeshes lost material slots");
         }},
        {"stale asset handles fail on subsequent extraction",
         [] {
             Fixture f;
             f.object();
             auto old = f.extractor.extract(f.scene, f.assets);
             f.assets.destroyMesh(f.mesh);
             throws([&] { f.extractor.extract(f.scene, f.assets); });
             require(f.frame(old).stats.drawCalls == 1, "Old snapshot must retain the old mesh");
         }},
        {"validation rejects unsupported and malformed assets",
         [] {
             AssetStore assets;
             auto mesh = geometry::cube();
             mesh.indices[0] = 9999;
             throws([&] { assets.createMesh(mesh); });
             throws([&] { assets.createTexture({2, 2, {1, 2, 3, 4}, true}); });
             throws([&] { assets.createMaterial({.baseColor = {1, 1, 1, 1.5f}}); });
             Scene s;
             Mat4 invalid = Mat4::identity();
             invalid(3, 0) = 1;
             throws([&] { s.setLocalTransform(s.root(), invalid); });
         }},
        {"deep trees update and destroy without recursion", [] {
             Scene s;
             auto first = s.createNode(s.root()), node = first;
             for (int i = 0; i < 12000; ++i)
                 node = s.createNode(node);
             s.setLocalTransform(first, Mat4::translation({2, 0, 0}));
             near(s.worldTransform(node)(0, 3), 2);
             s.destroySubtree(first);
             require(!s.valid(node), "Deep descendant survived deletion");
             s.worldTransform(s.root());
         }}};
    size_t failures = 0;
    for (const auto &[name, test] : tests) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception &e) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    }
    std::cout << tests.size() - failures << '/' << tests.size() << " tests passed\n";
    return failures ? 1 : 0;
}
