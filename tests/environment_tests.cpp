/** @file @brief 昼夜绕回、太阳/调色、天气过渡及公共场景应用测试。 */
#include "sprite_fixture.hpp"
#include <renderer/environment/environment.hpp>
using namespace sprite_test;
int main() {
    try {
        Fixture f;
        EnvironmentController e;
        auto day = e.state();
        e.setHour(0);
        auto night = e.state();
        require(day.lightIntensity > night.lightIntensity && day.ambient > night.ambient);
        e.advance(120);
        near(float(e.state().hour), 0);
        e.setWeather(Weather::Rain);
        e.advance(1);
        near(e.state().precipitation, .5f);
        e.setWeather(Weather::Clear);
        e.advance(1);
        near(e.state().precipitation, .25f);
        auto parent = f.scene.createNode(f.scene.root());
        f.scene.setLocalTransform(parent, Mat4::translation({4, 3, 2}) * Mat4::rotationY(1));
        auto sun = f.scene.createNode(parent);
        RenderView view;
        e.apply(f.scene, sun, view);
        require(f.scene.component<Light>(sun).has_value());
        near(view.ambientLight, e.state().ambient);
        const auto world = f.scene.worldTransform(sun);
        const auto direction = normalized(Vec3{-world(0, 2), -world(1, 2), -world(2, 2)});
        near(direction.x, e.state().lightDirection.x);
        near(direction.y, e.state().lightDirection.y);
        near(direction.z, e.state().lightDirection.z);
        throws([&] { e.advance(-1); });
        throws([] { EnvironmentController bad({.dayLength = 0}); });
        std::cout << "environment tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
