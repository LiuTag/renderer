/** @file @brief Box2D 安全句柄、固定步、值事件、查询与场景变换同步实现。 */
#include <algorithm>
#include <atomic>
#include <box2d/box2d.h>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <renderer/physics2d/physics2d.hpp>
#include <stdexcept>
#include <utility>
#include <variant>
namespace renderer {
namespace {
constexpr float pi = 3.14159265358979323846f;
void scalar(float v, float lo, float hi) {
    if (!std::isfinite(v) || v < lo || v > hi)
        throw std::invalid_argument("Physics scalar outside supported range");
}
void vector(Vec2 v, float limit = 1e6f) {
    scalar(v.x, -limit, limit);
    scalar(v.y, -limit, limit);
}
b2Vec2 native(Vec2 v) {
    return {v.x, v.y};
}
Vec2 value(b2Vec2 v) {
    return {v.x, v.y};
}
b2Rot nativeRotation(float a) {
    return {std::cos(a), std::sin(a)};
}
float rotationAngle(b2Rot rotation) {
    // 使用标准 atan2，避免物理库快速反三角近似在场景大偏移处放大可见误差。
    return std::atan2(rotation.s, rotation.c);
}
float angle(float a) {
    scalar(a, -1e6f, 1e6f);
    return std::remainder(a, 2 * pi);
}
Mat4 plane(Vec2 p, float a, bool rotation) {
    return Mat4::translation({p.x, 0, p.y}) * (rotation ? Mat4::rotationY(-a) : Mat4::identity());
}
using Geometry = std::variant<b2Circle, b2Capsule, b2Polygon>;
Geometry makeGeometry(const Shape2DDesc &d) {
    vector(d.offset, 1e4f);
    angle(d.angle);
    scalar(d.density, 0, 1e4f);
    scalar(d.friction, 0, 1e4f);
    scalar(d.restitution, 0, 1);
    const auto rotation = nativeRotation(angle(d.angle));
    auto point = [&](Vec2 p) {
        vector(p, 1e4f);
        return b2Add(native(d.offset), b2RotateVector(rotation, native(p)));
    };
    switch (d.type) {
    case Shape2DType::Circle:
        scalar(d.radius, .001f, 1e4f);
        return b2Circle{native(d.offset), d.radius};
    case Shape2DType::Box:
        scalar(d.halfExtents.x, .001f, 1e4f);
        scalar(d.halfExtents.y, .001f, 1e4f);
        return b2MakeOffsetBox(d.halfExtents.x, d.halfExtents.y, native(d.offset), rotation);
    case Shape2DType::Capsule: {
        scalar(d.radius, .001f, 1e4f);
        const auto a = point(d.pointA), b = point(d.pointB);
        if (b2DistanceSquared(a, b) < 1e-6f)
            throw std::invalid_argument("Degenerate capsule");
        return b2Capsule{a, b, d.radius};
    }
    case Shape2DType::Polygon: {
        if (d.vertices.size() < 3 || d.vertices.size() > B2_MAX_POLYGON_VERTICES)
            throw std::invalid_argument("Polygon needs 3 to 8 points");
        std::vector<b2Vec2> points;
        for (auto p : d.vertices)
            points.push_back(point(p));
        const auto hull = b2ComputeHull(points.data(), int(points.size()));
        if (hull.count < 3 || !b2ValidateHull(&hull))
            throw std::invalid_argument("Degenerate polygon hull");
        return b2MakePolygon(&hull, 0);
    }
    }
    throw std::invalid_argument("Invalid shape type");
}
} // namespace
struct Physics2D::Impl {
    struct Binding {
        NodeHandle node;
        Mat4 offset;
        bool rotation = false;
    };
    struct Body {
        uint64_t generation = 1, userData = 0;
        b2BodyId native{};
        std::vector<b2ShapeId> shapes;
        Vec2 previous{};
        float previousAngle = 0;
        std::optional<Binding> binding;
    };
    struct Joint {
        uint64_t generation = 1;
        b2JointId native{};
        Body2DHandle a, b;
    };
    struct ShapeRef {
        Body2DHandle body;
        uint32_t index;
        uint64_t userData;
    };
    Physics2DConfig config;
    b2WorldId world{};
    uint64_t owner;
    std::vector<Body> bodies;
    std::vector<Joint> joints;
    std::vector<uint32_t> freeBodies, freeJoints;
    std::map<uint64_t, ShapeRef> shapes;
    std::vector<uint64_t> retiredShapes;
    std::vector<Physics2DEvent> events;
    Physics2DStats stats;
    double remainder = 0, alpha = 1;
    explicit Impl(const Physics2DConfig &c) : config(c) {
        vector(c.gravity, 1e5f);
        if (!std::isfinite(c.fixedTimeStep) || c.fixedTimeStep < 1.0 / 1000 ||
            c.fixedTimeStep > .1 || !c.subSteps || c.subSteps > 16 || !c.maxStepsPerAdvance ||
            c.maxStepsPerAdvance > 64 || !c.workerCount || c.workerCount > 16)
            throw std::invalid_argument("Invalid physics step/thread configuration");
        static std::atomic<uint64_t> next{1};
        owner = next++;
        auto d = b2DefaultWorldDef();
        d.gravity = native(c.gravity);
        d.workerCount = int(c.workerCount);
        world = b2CreateWorld(&d);
        if (B2_IS_NULL(world))
            throw std::runtime_error("Cannot create Box2D world");
    }
    ~Impl() {
        b2DestroyWorld(world);
    }
    bool valid(Body2DHandle h) const {
        return h.owner == owner && h.index < bodies.size() &&
               h.generation == bodies[h.index].generation && B2_IS_NON_NULL(bodies[h.index].native);
    }
    bool valid(Joint2DHandle h) const {
        return h.owner == owner && h.index < joints.size() &&
               h.generation == joints[h.index].generation && B2_IS_NON_NULL(joints[h.index].native);
    }
    Body &get(Body2DHandle h) {
        if (!valid(h))
            throw std::invalid_argument("Stale/foreign physics body");
        return bodies[h.index];
    }
    const Body &get(Body2DHandle h) const {
        if (!valid(h))
            throw std::invalid_argument("Stale/foreign physics body");
        return bodies[h.index];
    }
    template <class Slot> uint32_t allocate(std::vector<Slot> &slots, std::vector<uint32_t> &free) {
        if (!free.empty()) {
            const auto i = free.back();
            free.pop_back();
            return i;
        }
        if (slots.size() >= UINT32_MAX)
            throw std::length_error("Too many physics slots");
        // 提前预留回收容量，销毁原生对象之后不再因 free-list 扩容而失败。
        if (free.capacity() < slots.size() + 1)
            free.reserve(std::max(slots.size() + 1, free.capacity() * 2));
        slots.emplace_back();
        return uint32_t(slots.size() - 1);
    }
    template <class Slot> void retire(Slot &slot, uint32_t index, std::vector<uint32_t> &free) {
        if (slot.generation != UINT64_MAX) {
            free.push_back(index);
            ++slot.generation;
        }
        slot.native = {};
    }
    void event(Physics2DEventKind kind, b2ShapeId a, b2ShapeId b, Vec2 point = {}, Vec2 normal = {},
               float speed = 0) {
        const auto x = shapes.find(b2StoreShapeId(a)), y = shapes.find(b2StoreShapeId(b));
        if (x == shapes.end() || y == shapes.end())
            return;
        events.push_back({kind, x->second.body, y->second.body, x->second.index, y->second.index,
                          x->second.userData, y->second.userData, point, normal, speed});
    }
    void simulate() {
        for (auto &b : bodies)
            if (B2_IS_NON_NULL(b.native)) {
                b.previous = value(b2Body_GetPosition(b.native));
                b.previousAngle = rotationAngle(b2Body_GetRotation(b.native));
            }
        b2World_Step(world, float(config.fixedTimeStep), int(config.subSteps));
        const auto contact = b2World_GetContactEvents(world);
        for (int i = 0; i < contact.beginCount; ++i)
            event(Physics2DEventKind::ContactBegin, contact.beginEvents[i].shapeIdA,
                  contact.beginEvents[i].shapeIdB);
        for (int i = 0; i < contact.endCount; ++i)
            event(Physics2DEventKind::ContactEnd, contact.endEvents[i].shapeIdA,
                  contact.endEvents[i].shapeIdB);
        for (int i = 0; i < contact.hitCount; ++i) {
            const auto &hit = contact.hitEvents[i];
            event(Physics2DEventKind::Hit, hit.shapeIdA, hit.shapeIdB, value(hit.point),
                  value(hit.normal), hit.approachSpeed);
        }
        const auto sensor = b2World_GetSensorEvents(world);
        for (int i = 0; i < sensor.beginCount; ++i)
            event(Physics2DEventKind::SensorBegin, sensor.beginEvents[i].sensorShapeId,
                  sensor.beginEvents[i].visitorShapeId);
        for (int i = 0; i < sensor.endCount; ++i)
            event(Physics2DEventKind::SensorEnd, sensor.endEvents[i].sensorShapeId,
                  sensor.endEvents[i].visitorShapeId);
        for (auto id : retiredShapes)
            shapes.erase(id);
        retiredShapes.clear();
    }
    struct Query {
        const Impl &world;
        const Physics2DQueryFilter &filter;
        std::optional<Physics2DHit> hit;
        std::vector<Body2DHandle> overlap;
        const ShapeRef *accept(b2ShapeId id) const {
            const auto found = world.shapes.find(b2StoreShapeId(id));
            if (found == world.shapes.end() || found->second.body == filter.ignore ||
                (!filter.includeSensors && b2Shape_IsSensor(id)))
                return nullptr;
            return &found->second;
        }
    };
};
Physics2D::Physics2D(const Physics2DConfig &c) : impl_(std::make_unique<Impl>(c)) {}
Physics2D::~Physics2D() = default;
Physics2D::Physics2D(Physics2D &&) noexcept = default;
Physics2D &Physics2D::operator=(Physics2D &&) noexcept = default;
bool Physics2D::valid(Body2DHandle b) const {
    return impl_->valid(b);
}
bool Physics2D::valid(Joint2DHandle j) const {
    return impl_->valid(j);
}
Physics2DStats Physics2D::statistics() const {
    return impl_->stats;
}
Body2DHandle Physics2D::createBody(const Body2DDesc &d) {
    vector(d.position);
    vector(d.linearVelocity, 1e5f);
    angle(d.angle);
    scalar(d.angularVelocity, -1e5f, 1e5f);
    scalar(d.gravityScale, -1000, 1000);
    scalar(d.linearDamping, 0, 1e4f);
    scalar(d.angularDamping, 0, 1e4f);
    if ((d.type != Body2DType::Static && d.type != Body2DType::Kinematic &&
         d.type != Body2DType::Dynamic) ||
        d.shapes.size() > 64)
        throw std::invalid_argument("Invalid body type or shape count");
    std::vector<Geometry> prepared;
    for (const auto &s : d.shapes)
        prepared.push_back(makeGeometry(s));
    auto def = b2DefaultBodyDef();
    def.type = d.type == Body2DType::Static      ? b2_staticBody
               : d.type == Body2DType::Kinematic ? b2_kinematicBody
                                                 : b2_dynamicBody;
    def.position = native(d.position);
    def.rotation = nativeRotation(angle(d.angle));
    def.linearVelocity = native(d.linearVelocity);
    def.angularVelocity = d.angularVelocity;
    def.linearDamping = d.linearDamping;
    def.angularDamping = d.angularDamping;
    def.gravityScale = d.gravityScale;
    def.motionLocks.angularZ = d.lockRotation;
    def.isBullet = d.bullet;
    def.enableSleep = d.allowSleep;
    def.isEnabled = d.enabled;
    auto &p = *impl_;
    const auto index = p.allocate(p.bodies, p.freeBodies);
    auto &body = p.bodies[index];
    Body2DHandle handle{index, body.generation, p.owner};
    body.userData = d.userData;
    body.previous = d.position;
    body.previousAngle = angle(d.angle);
    body.native = b2CreateBody(p.world, &def);
    try {
        body.shapes.reserve(prepared.size());
        for (size_t i = 0; i < prepared.size(); ++i) {
            const auto &shape = d.shapes[i];
            auto sd = b2DefaultShapeDef();
            sd.density = shape.density;
            sd.material.friction = shape.friction;
            sd.material.restitution = shape.restitution;
            sd.filter.categoryBits = shape.categoryBits;
            sd.filter.maskBits = shape.maskBits;
            sd.isSensor = shape.sensor;
            sd.enableSensorEvents = true;
            sd.enableContactEvents = shape.contactEvents;
            sd.enableHitEvents = shape.hitEvents;
            const auto id = std::visit(
                [&](const auto &g) {
                    using G = std::decay_t<decltype(g)>;
                    if constexpr (std::is_same_v<G, b2Circle>)
                        return b2CreateCircleShape(body.native, &sd, &g);
                    else if constexpr (std::is_same_v<G, b2Capsule>)
                        return b2CreateCapsuleShape(body.native, &sd, &g);
                    else
                        return b2CreatePolygonShape(body.native, &sd, &g);
                },
                prepared[i]);
            body.shapes.push_back(id);
            p.shapes.emplace(b2StoreShapeId(id), Impl::ShapeRef{handle, uint32_t(i), d.userData});
        }
    } catch (...) {
        for (auto id : body.shapes)
            p.shapes.erase(b2StoreShapeId(id));
        body.shapes.clear();
        b2DestroyBody(body.native);
        p.retire(body, index, p.freeBodies);
        throw;
    }
    ++p.stats.bodies;
    p.stats.shapes += uint32_t(body.shapes.size());
    return handle;
}
void Physics2D::destroyJoint(Joint2DHandle j) {
    auto &p = *impl_;
    if (!p.valid(j))
        throw std::invalid_argument("Stale/foreign physics joint");
    auto &slot = p.joints[j.index];
    b2DestroyJoint(slot.native);
    p.retire(slot, j.index, p.freeJoints);
    --p.stats.joints;
}
void Physics2D::destroyBody(Body2DHandle h) {
    auto &p = *impl_;
    auto &b = p.get(h);
    p.retiredShapes.reserve(p.retiredShapes.size() + b.shapes.size());
    for (uint32_t i = 0; i < p.joints.size(); ++i) {
        const auto &j = p.joints[i];
        if (B2_IS_NON_NULL(j.native) && (j.a == h || j.b == h))
            destroyJoint({i, j.generation, p.owner});
    }
    // Box2D 会在下一个物理步报告已删除形状的退出事件，因此暂留应用身份。
    for (auto shape : b.shapes)
        p.retiredShapes.push_back(b2StoreShapeId(shape));
    b2DestroyBody(b.native);
    p.stats.shapes -= uint32_t(b.shapes.size());
    --p.stats.bodies;
    b.shapes.clear();
    b.binding.reset();
    p.retire(b, h.index, p.freeBodies);
}
Body2DState Physics2D::state(Body2DHandle h) const {
    const auto id = impl_->get(h).native;
    return {value(b2Body_GetPosition(id)),
            value(b2Body_GetLinearVelocity(id)),
            rotationAngle(b2Body_GetRotation(id)),
            b2Body_GetAngularVelocity(id),
            b2Body_GetMass(id),
            b2Body_IsAwake(id),
            b2Body_IsEnabled(id)};
}
void Physics2D::setTransform(Body2DHandle h, Vec2 position, float a) {
    vector(position);
    a = angle(a);
    auto &b = impl_->get(h);
    b2Body_SetTransform(b.native, native(position), nativeRotation(a));
    b.previous = position;
    b.previousAngle = a;
}
void Physics2D::setVelocity(Body2DHandle h, Vec2 linear, float angular) {
    vector(linear, 1e5f);
    scalar(angular, -1e5f, 1e5f);
    const auto id = impl_->get(h).native;
    b2Body_SetLinearVelocity(id, native(linear));
    b2Body_SetAngularVelocity(id, angular);
}
void Physics2D::setEnabled(Body2DHandle h, bool enabled) {
    const auto id = impl_->get(h).native;
    if (enabled)
        b2Body_Enable(id);
    else
        b2Body_Disable(id);
}
void Physics2D::applyForce(Body2DHandle h, Vec2 force, std::optional<Vec2> point, bool wake) {
    vector(force, 1e12f);
    if (point)
        vector(*point);
    const auto id = impl_->get(h).native;
    b2Body_ApplyForce(id, native(force), point ? native(*point) : b2Body_GetWorldCenter(id), wake);
}
void Physics2D::applyImpulse(Body2DHandle h, Vec2 impulse, std::optional<Vec2> point, bool wake) {
    vector(impulse, 1e12f);
    if (point)
        vector(*point);
    const auto id = impl_->get(h).native;
    b2Body_ApplyLinearImpulse(id, native(impulse),
                              point ? native(*point) : b2Body_GetWorldCenter(id), wake);
}
Physics2DAdvance Physics2D::advance(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 4)
        throw std::invalid_argument("Physics delta must be in [0,4]");
    auto &p = *impl_;
    const auto start = std::chrono::steady_clock::now();
    Physics2DAdvance result;
    const double limit = p.config.fixedTimeStep * p.config.maxStepsPerAdvance,
                 total = p.remainder + seconds;
    result.droppedSeconds = std::max(0.0, total - limit);
    p.remainder = std::min(total, limit);
    while (p.remainder + 1e-12 >= p.config.fixedTimeStep &&
           result.steps < p.config.maxStepsPerAdvance) {
        p.simulate();
        p.remainder = std::max(0.0, p.remainder - p.config.fixedTimeStep);
        ++result.steps;
    }
    result.interpolationAlpha = p.alpha =
        std::clamp(p.remainder / p.config.fixedTimeStep, 0.0, 1.0);
    p.stats.lastAdvance = result;
    p.stats.simulationMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}
void Physics2D::step() {
    auto &p = *impl_;
    const auto start = std::chrono::steady_clock::now();
    p.simulate();
    p.alpha = 1;
    p.stats.lastAdvance = {1, 1, 0};
    p.stats.simulationMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
std::vector<Physics2DEvent> Physics2D::takeEvents() {
    return std::exchange(impl_->events, {});
}
std::optional<Physics2DHit> Physics2D::rayCast(Vec2 origin, Vec2 translation,
                                               const Physics2DQueryFilter &filter) const {
    vector(origin);
    vector(translation);
    if (std::hypot(translation.x, translation.y) < 1e-8f)
        throw std::invalid_argument("Empty physics ray");
    Impl::Query query{*impl_, filter};
    auto f = b2DefaultQueryFilter();
    f.categoryBits = filter.categoryBits;
    f.maskBits = filter.maskBits;
    b2World_CastRay(
        impl_->world, native(origin), native(translation), f,
        [](b2ShapeId shape, b2Pos point, b2Vec2 normal, float fraction, void *context) {
            auto &q = *static_cast<Impl::Query *>(context);
            const auto *ref = q.accept(shape);
            if (!ref)
                return -1.f;
            if (!q.hit || fraction < q.hit->fraction)
                q.hit = Physics2DHit{ref->body,    ref->index,    ref->userData,
                                     value(point), value(normal), fraction};
            return q.hit->fraction;
        },
        &query);
    return query.hit;
}
std::vector<Body2DHandle> Physics2D::overlapCircle(Vec2 center, float radius,
                                                   const Physics2DQueryFilter &filter) const {
    vector(center);
    scalar(radius, .001f, 1e4f);
    Impl::Query query{*impl_, filter};
    query.overlap.reserve(impl_->stats.shapes);
    auto f = b2DefaultQueryFilter();
    f.categoryBits = filter.categoryBits;
    f.maskBits = filter.maskBits;
    const b2Vec2 zero{};
    const auto proxy = b2MakeProxy(&zero, 1, radius);
    b2World_OverlapShape(
        impl_->world, native(center), &proxy, f,
        [](b2ShapeId shape, void *context) {
            auto &q = *static_cast<Impl::Query *>(context);
            if (const auto *ref = q.accept(shape))
                q.overlap.push_back(ref->body);
            return true;
        },
        &query);
    auto &out = query.overlap;
    std::sort(out.begin(), out.end(), [](auto a, auto b) { return a.index < b.index; });
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
Joint2DHandle Physics2D::createDistanceJoint(const DistanceJoint2DDesc &d) {
    auto &p = *impl_;
    const auto a = p.get(d.a).native, b = p.get(d.b).native;
    if (d.a == d.b)
        throw std::invalid_argument("Joint needs different bodies");
    vector(d.anchorA, 1e4f);
    vector(d.anchorB, 1e4f);
    scalar(d.length, .01f, 1e6f);
    scalar(d.minLength, .01f, 1e6f);
    scalar(d.maxLength, d.minLength, 1e6f);
    scalar(d.hertz, 0, 1000);
    scalar(d.dampingRatio, 0, 10);
    auto def = b2DefaultDistanceJointDef();
    def.base.bodyIdA = a;
    def.base.bodyIdB = b;
    def.base.localFrameA.p = native(d.anchorA);
    def.base.localFrameB.p = native(d.anchorB);
    def.base.collideConnected = d.collideConnected;
    def.length = d.length;
    def.enableSpring = d.spring;
    def.enableLimit = d.limit;
    def.minLength = d.minLength;
    def.maxLength = d.maxLength;
    def.hertz = d.hertz;
    def.dampingRatio = d.dampingRatio;
    const auto index = p.allocate(p.joints, p.freeJoints);
    auto &j = p.joints[index];
    j.a = d.a;
    j.b = d.b;
    j.native = b2CreateDistanceJoint(p.world, &def);
    ++p.stats.joints;
    return {index, j.generation, p.owner};
}
void Physics2D::bindNode(Body2DHandle h, Scene &scene, NodeHandle node, bool rotation) {
    auto &p = *impl_;
    auto &b = p.get(h);
    if (!scene.valid(node) || node == scene.root())
        throw std::invalid_argument("Cannot bind invalid/root scene node");
    for (const auto &other : p.bodies)
        if (other.binding && other.binding->node == node && &other != &b)
            throw std::invalid_argument("Scene node already bound");
    const auto s = state(h);
    const auto transform = plane(s.position, s.angle, rotation);
    b.binding = Impl::Binding{node, *transform.inverse() * scene.worldTransform(node), rotation};
    b.previous = s.position;
    b.previousAngle = s.angle;
}
void Physics2D::unbindNode(Body2DHandle h) {
    impl_->get(h).binding.reset();
}
void Physics2D::synchronize(Scene &scene, bool interpolate) {
    struct Update {
        NodeHandle node;
        Mat4 world;
        size_t depth;
    };
    std::vector<Update> updates;
    for (auto &body : impl_->bodies)
        if (body.binding && body.binding->node.owner == scene.root().owner) {
            const auto binding = *body.binding;
            if (!scene.valid(binding.node)) {
                body.binding.reset();
                continue;
            }
            auto p = value(b2Body_GetPosition(body.native));
            auto a = rotationAngle(b2Body_GetRotation(body.native));
            if (interpolate) {
                const auto t = float(impl_->alpha);
                p = {std::lerp(body.previous.x, p.x, t), std::lerp(body.previous.y, p.y, t)};
                a = body.previousAngle + std::remainder(a - body.previousAngle, 2 * pi) * t;
            }
            size_t depth = 0;
            for (auto parent = scene.parent(binding.node); parent; parent = scene.parent(parent))
                ++depth;
            updates.push_back(
                {binding.node, plane(p, a, binding.rotation) * binding.offset, depth});
        }
    std::stable_sort(updates.begin(), updates.end(),
                     [](const auto &a, const auto &b) { return a.depth < b.depth; });
    for (const auto &u : updates) {
        auto local = u.world;
        if (const auto parent = scene.parent(u.node)) {
            const auto inverse = scene.worldTransform(parent).inverse();
            if (!inverse)
                throw std::invalid_argument("Physics node parent transform is singular");
            local = *inverse * local;
        }
        scene.setLocalTransform(u.node, local);
    }
}
} // namespace renderer
