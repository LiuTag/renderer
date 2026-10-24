/** @file @brief 真实 Box2D 响应、事件、查询、固定步、关节、句柄和场景同步回归。 */
#include "sprite_fixture.hpp"
#include <renderer/physics2d/physics2d.hpp>
using namespace sprite_test;
namespace {
Body2DDesc circle(Vec2 p, Body2DType type = Body2DType::Dynamic) {
    Body2DDesc d;
    d.position = p;
    d.type = type;
    d.lockRotation = true;
    return d;
}
void handlesAndShapes() {
    Physics2D world, other;
    auto body = world.createBody(circle({}));
    require(world.valid(body) && !other.valid(body));
    near(world.state(body).mass, 3.14159265f * .25f, .001f);
    world.destroyBody(body);
    require(!world.valid(body));
    throws([&] { world.state(body); });
    auto newer = world.createBody(circle({}));
    require(newer.index == body.index && newer.generation != body.generation);
    throws([&] { world.destroyBody(body); });
    for (auto type :
         {Shape2DType::Circle, Shape2DType::Box, Shape2DType::Capsule, Shape2DType::Polygon}) {
        auto d = circle({3, 3});
        d.shapes[0].type = type;
        d.shapes[0].vertices = {{-.5f, -.5f}, {.5f, -.5f}, {.5f, .5f}, {-.5f, .5f}};
        auto h = world.createBody(d);
        require(world.state(h).mass > 0);
        world.destroyBody(h);
    }
    auto bad = circle({});
    bad.shapes[0].type = Shape2DType::Polygon;
    bad.shapes[0].vertices = {{0, 0}, {1, 1}, {2, 2}};
    throws([&] { world.createBody(bad); });
    require(world.statistics().bodies == 1);
    bad = circle({});
    bad.shapes[0].radius = 0;
    throws([&] { world.createBody(bad); });
    Physics2D moved = std::move(world);
    require(moved.valid(newer));
}
void fixedStepAndForces() {
    Physics2D a, b({.workerCount = 2});
    auto x = a.createBody(circle({})), y = b.createBody(circle({}));
    a.setVelocity(x, {3, 0});
    b.setVelocity(y, {3, 0});
    a.advance(1.0 / 30);
    b.advance(1.0 / 60);
    b.advance(1.0 / 60);
    near(a.state(x).position.x, b.state(y).position.x);
    const auto old = a.state(x).linearVelocity.x;
    a.applyImpulse(x, {1, 0});
    require(a.state(x).linearVelocity.x > old);
    a.applyForce(x, {2, 0});
    a.step();
    require(a.state(x).linearVelocity.x > old);
    const auto over = a.advance(4);
    require(over.steps == 8 && over.droppedSeconds > 3 && over.interpolationAlpha <= 1);
    a.setEnabled(x, false);
    auto p = a.state(x).position;
    a.step();
    near(a.state(x).position.x, p.x);
    a.setEnabled(x, true);
    require(a.state(x).enabled);
    Physics2D gravity({.gravity = {0, -10}});
    auto fall = gravity.createBody(circle({}));
    gravity.step();
    require(gravity.state(fall).position.y < 0);
    throws([&] { a.advance(-1); });
    throws([&] { a.setVelocity(x, {std::numeric_limits<float>::infinity(), 0}); });
}
void collisionAndEvents() {
    Physics2D world;
    auto wall = circle({2, 0}, Body2DType::Static);
    wall.shapes[0].type = Shape2DType::Box;
    wall.shapes[0].halfExtents = {.2f, 3};
    world.createBody(wall);
    auto actor = world.createBody(circle({}));
    world.setVelocity(actor, {10, 0});
    bool begin = false;
    for (int i = 0; i < 60; ++i) {
        world.step();
        for (const auto &e : world.takeEvents())
            begin |= e.kind == Physics2DEventKind::ContactBegin;
    }
    require(begin && world.state(actor).position.x < 1.4f);
    auto sensor = circle({-3, 0}, Body2DType::Static);
    sensor.userData = 123;
    sensor.shapes[0].sensor = true;
    const auto trigger = world.createBody(sensor);
    world.setTransform(actor, {-3, 0});
    world.setVelocity(actor, {});
    world.step();
    bool enter = false;
    for (const auto &e : world.takeEvents())
        enter |= e.kind == Physics2DEventKind::SensorBegin && e.a == trigger && e.b == actor &&
                 e.userDataA == 123;
    require(enter);
    world.destroyBody(trigger);
    world.step();
    bool leave = false;
    for (const auto &e : world.takeEvents())
        leave |= e.kind == Physics2DEventKind::SensorEnd && e.a == trigger && e.b == actor;
    require(leave && !world.valid(trigger));
    Physics2D continuous;
    wall.position = {1, 0};
    wall.shapes[0].halfExtents = {.02f, 2};
    continuous.createBody(wall);
    auto fast = circle({});
    fast.shapes[0].radius = .1f;
    fast.shapes[0].hitEvents = true;
    fast.bullet = true;
    const auto bullet = continuous.createBody(fast);
    continuous.setVelocity(bullet, {100, 0});
    bool hit = false;
    for (int i = 0; i < 3; ++i) {
        continuous.step();
        for (const auto &e : continuous.takeEvents())
            hit |= e.kind == Physics2DEventKind::Hit && e.approachSpeed > 0;
    }
    require(continuous.state(bullet).position.x < .95f && hit);
}
void queriesAndJoints() {
    Physics2D world;
    auto d = circle({2, 0}, Body2DType::Static);
    d.userData = 42;
    d.shapes[0].categoryBits = 2;
    const auto target = world.createBody(d);
    auto h = world.rayCast({0, 0}, {5, 0});
    require(h && h->body == target && h->userData == 42);
    near(h->fraction, .3f, .01f);
    require(!world.rayCast({0, 0}, {5, 0}, {.maskBits = 1}));
    require(!world.rayCast({0, 0}, {5, 0}, {.ignore = target}));
    require(world.overlapCircle({2, 0}, .1f).size() == 1);
    d.position = {4, 0};
    d.shapes[0].sensor = true;
    auto sensor = world.createBody(d);
    require(world.overlapCircle({4, 0}, .1f).empty());
    require(world.overlapCircle({4, 0}, .1f, {.includeSensors = true})[0] == sensor);
    auto dynamic = world.createBody(circle({3, 2}));
    auto joint = world.createDistanceJoint({.a = target, .b = dynamic, .length = 1});
    world.applyImpulse(dynamic, {5, 0});
    for (int i = 0; i < 60; ++i)
        world.step();
    const auto p = world.state(dynamic).position;
    near(std::hypot(p.x - 2, p.y), 1, .03f);
    world.destroyBody(dynamic);
    require(!world.valid(joint));
    throws([&] { world.destroyJoint(joint); });
}
void synchronization() {
    Scene scene;
    auto parent = scene.createNode(scene.root()), child = scene.createNode(parent);
    scene.setLocalTransform(parent, Mat4::translation({3, 2, 4}) * Mat4::rotationY(.3f));
    scene.setLocalTransform(child, Mat4::translation({1, .5f, 2}) * Mat4::scaling({2, 3, 1}));
    Physics2D world;
    auto body = world.createBody(circle({}));
    const auto before = scene.worldTransform(child);
    world.bindNode(body, scene, child, true);
    world.setTransform(body, {2, 3}, .5f);
    world.synchronize(scene, false);
    const auto expected = Mat4::translation({2, 0, 3}) * Mat4::rotationY(-.5f) * before,
               actual = scene.worldTransform(child);
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (std::abs(actual(r, c) - expected(r, c)) > .001f)
                throw std::runtime_error("Sync matrix mismatch " + std::to_string(actual(r, c)) +
                                         " vs " + std::to_string(expected(r, c)));
    world.unbindNode(body);
    world.setTransform(body, {});
    world.bindNode(body, scene, child, false);
    world.setVelocity(body, {6, 0});
    world.advance(1.5 / 60);
    world.synchronize(scene, true);
    const auto interpolated = scene.worldTransform(child)(0, 3);
    if (std::abs(interpolated - (actual(0, 3) + .05f)) > .002f)
        throw std::runtime_error("Interpolation mismatch " + std::to_string(interpolated) + " vs " +
                                 std::to_string(actual(0, 3) + .05f));
    scene.destroySubtree(parent);
    world.synchronize(scene);
    require(world.valid(body));
    throws([&] { world.bindNode(body, scene, scene.root()); });
}
} // namespace
int main() {
    try {
        for (const auto &[name, test] : std::vector<std::pair<const char *, std::function<void()>>>{
                 {"handles/shapes", handlesAndShapes},
                 {"fixed step/forces", fixedStepAndForces},
                 {"collision/events", collisionAndEvents},
                 {"queries/joints", queriesAndJoints},
                 {"scene synchronization", synchronization}}) {
            std::cout << "RUN " << name << '\n';
            test();
        }
        std::cout << "5 physics2d scenarios passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
