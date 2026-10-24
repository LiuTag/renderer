/** @file @brief 批次资源独立性、不可变快照、整块裁剪与旧数据回滚测试。 */
#include "sprite_fixture.hpp"
using namespace sprite_test;
int main() {
    try {
        Fixture f;
        SpriteBatch batch;
        BatchSprite a{SpriteRenderer{.sprite = f.first}, Mat4::identity(), 42};
        batch.replace(f.assets, std::span{&a, 1});
        auto old = f.extractor.extract(f.scene, f.assets, std::span{&batch, 1});
        require(old.spriteCount() == 1);
        auto view = old.spriteView({.cameraNode = f.camera}, 200, 200);
        require(view.sprites()[0].instanceId == 42 && !view.sprites()[0].node);
        a.world = Mat4::translation({1000, 0, 0});
        batch.replace(f.assets, std::span{&a, 1});
        auto next = f.extractor.extract(f.scene, f.assets, std::span{&batch, 1});
        auto prepared =
            detail::prepareFrame(next, {.cameraNode = f.camera, .shadows = false}, 200, 200);
        require(prepared.stats.visibleInstances == 0 && prepared.stats.culledInstances == 1);
        require(old.spriteView({.cameraNode = f.camera}, 200, 200).sprites().size() == 1);
        // Fixed 图片分块界限应与真实几何一致，避免地图平面被无谓扩大成球。
        BatchSprite fixed{SpriteRenderer{.sprite = f.first, .billboard = BillboardMode::Fixed},
                          Mat4::rotationX(-1.5707963f), 7};
        SpriteBatch floor;
        floor.replace(f.assets, std::span{&fixed, 1});
        near(floor.bounds().minimum.y, 0);
        near(floor.bounds().maximum.y, 0);
        // 不同相机朝向下的 Billboard 几何均必须落在已发布的保守包围盒内。
        fixed.renderer.billboard = BillboardMode::ViewPlane;
        fixed.renderer.rotation = .7f;
        fixed.world = Mat4::scaling({2, .5f, 1});
        floor.replace(f.assets, std::span{&fixed, 1});
        const auto bound = floor.bounds();
        auto dynamic = f.extractor.extract(f.scene, f.assets, std::span{&floor, 1});
        for (const auto camera : {Vec3{0, 0, 10}, Vec3{10, 5, 2}}) {
            f.scene.setLocalTransform(f.camera, *Mat4::lookAt(camera, {}).inverse());
            dynamic = f.extractor.extract(f.scene, f.assets, std::span{&floor, 1});
            const auto geometry = dynamic.spriteView({.cameraNode = f.camera}, 200, 200);
            require(geometry.sprites().size() == 1);
            const auto actual = geometry.sprites()[0].world;
            for (float x : {-.5f, .5f})
                for (float y : {-.5f, .5f}) {
                    const auto point = actual.transformPoint({x, y, 0});
                    require(point.x >= bound.minimum.x && point.x <= bound.maximum.x &&
                            point.y >= bound.minimum.y && point.y <= bound.maximum.y &&
                            point.z >= bound.minimum.z && point.z <= bound.maximum.z);
                }
        }
        a.renderer.tint.w = 2;
        throws([&] { batch.replace(f.assets, std::span{&a, 1}); });
        require(batch.size() == 1);
        f.assets.destroySprite(f.first);
        require(old.spriteView({.cameraNode = f.camera}, 200, 200).sprites().size() == 1);
        std::cout << "batch tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
