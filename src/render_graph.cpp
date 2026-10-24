#include "renderer/render_graph.hpp"
#include "internal/pool.hpp"
#include "internal/rhi_backend.hpp"
#include <algorithm>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <variant>

namespace renderer {
namespace {
bool writes(rhi::Access a) {
    return a == rhi::Access::StorageWrite || a == rhi::Access::StorageReadWrite ||
           a == rhi::Access::TransferWrite || a == rhi::Access::ColorWrite ||
           a == rhi::Access::DepthWrite;
}
bool reads(rhi::Access a) {
    return a != rhi::Access::None && (!writes(a) || a == rhi::Access::StorageReadWrite);
}
struct Resource {
    std::variant<rhi::TextureDesc, rhi::BufferDesc> desc;
    rhi::TextureHandle texture;
    rhi::BufferHandle buffer;
    rhi::Access initial = rhi::Access::None;
    bool imported = false;
};
struct Pass {
    std::string name;
    std::vector<GraphAccess> accesses;
    GraphCallback callback;
    bool sideEffect;
    std::set<uint32_t> dependencies;
};
bool same(const Resource &a, const Resource &b) {
    if (a.desc.index() != b.desc.index())
        return false;
    if (const auto *x = std::get_if<rhi::TextureDesc>(&a.desc)) {
        const auto &y = std::get<rhi::TextureDesc>(b.desc);
        return x->width == y.width && x->height == y.height && x->mipLevels == y.mipLevels &&
               x->format == y.format;
    }
    const auto &x = std::get<rhi::BufferDesc>(a.desc);
    const auto &y = std::get<rhi::BufferDesc>(b.desc);
    return x.size == y.size && x.memory == y.memory;
}
std::pair<size_t, size_t> interval(const Resource &r, const GraphRange &range) {
    const bool t = r.desc.index() == 0;
    const size_t n =
        t ? std::get<rhi::TextureDesc>(r.desc).mipLevels : std::get<rhi::BufferDesc>(r.desc).size;
    const size_t first = t ? range.firstMip : range.offset, count = t ? range.mipCount : range.size;
    if (first >= n || (count && count > n - first))
        throw std::invalid_argument("RenderGraph access range exceeds resource");
    return {first, count ? first + count : n};
}
bool overlaps(const Resource &r, const GraphRange &a, const GraphRange &b) {
    auto x = interval(r, a), y = interval(r, b);
    return x.first < y.second && y.first < x.second;
}
} // namespace
struct RenderGraph::Impl {
    uint64_t owner = detail::uniqueId();
    std::vector<Resource> resources;
    std::vector<Pass> passes;
    std::set<uint32_t> exports;
    uint32_t index(GraphResource h) const {
        if (h.owner != owner || h.generation != 1 || h.index >= resources.size())
            throw std::invalid_argument("Invalid/cross-graph resource");
        return h.index;
    }
    GraphResource add(Resource r) {
        auto i = static_cast<uint32_t>(resources.size());
        resources.push_back(std::move(r));
        return {i, 1, owner};
    }
};
struct GraphResources::Impl {
    uint64_t owner;
    std::vector<rhi::TextureHandle> textures;
    std::vector<rhi::BufferHandle> buffers;
    std::set<uint32_t> allowed;
};
GraphResources::GraphResources(std::unique_ptr<Impl> p) : impl_(std::move(p)) {}
GraphResources::~GraphResources() = default;
GraphResources::GraphResources(GraphResources &&) noexcept = default;
GraphResources &GraphResources::operator=(GraphResources &&) noexcept = default;
rhi::TextureHandle GraphResources::texture(GraphResource h) const {
    if (h.owner != impl_->owner || h.generation != 1 || !impl_->allowed.contains(h.index) ||
        h.index >= impl_->textures.size() || !impl_->textures[h.index])
        throw std::invalid_argument("Graph resource is not an instantiated texture");
    return impl_->textures[h.index];
}
rhi::BufferHandle GraphResources::buffer(GraphResource h) const {
    if (h.owner != impl_->owner || h.generation != 1 || !impl_->allowed.contains(h.index) ||
        h.index >= impl_->buffers.size() || !impl_->buffers[h.index])
        throw std::invalid_argument("Graph resource is not an instantiated buffer");
    return impl_->buffers[h.index];
}
struct CompiledRenderGraph::Impl {
    uint64_t owner = 0;
    std::vector<Resource> resources, allocations;
    std::vector<Pass> passes;
    std::vector<std::vector<std::pair<uint32_t, rhi::ResourceBarrier>>> barriers;
    std::vector<int> physical;
    std::unique_ptr<GraphResources> resolved;
    std::shared_ptr<rhi::detail::DeviceBackend> device;
    GraphStatistics stats;
    ~Impl() {
        if (device)
            for (auto &a : allocations) {
                if (a.texture)
                    device->destroy(a.texture);
                if (a.buffer)
                    device->destroy(a.buffer);
            }
    }
};
RenderGraph::RenderGraph() : impl_(std::make_unique<Impl>()) {}
RenderGraph::~RenderGraph() = default;
RenderGraph::RenderGraph(RenderGraph &&) noexcept = default;
RenderGraph &RenderGraph::operator=(RenderGraph &&) noexcept = default;
GraphResource RenderGraph::createTexture(const rhi::TextureDesc &d) {
    if (!d.width || !d.height || !d.mipLevels)
        throw std::invalid_argument("Invalid graph texture");
    return impl_->add({d, {}, {}, rhi::Access::None, false});
}
GraphResource RenderGraph::createBuffer(const rhi::BufferDesc &d) {
    if (!d.size)
        throw std::invalid_argument("Invalid graph buffer");
    return impl_->add({d, {}, {}, rhi::Access::None, false});
}
GraphResource RenderGraph::importTexture(rhi::TextureHandle h, const rhi::TextureDesc &d,
                                         rhi::Access a) {
    if (!h)
        throw std::invalid_argument("Invalid imported texture");
    auto r = createTexture(d);
    auto &v = impl_->resources[r.index];
    v.texture = h;
    v.imported = true;
    v.initial = a;
    return r;
}
GraphResource RenderGraph::importBuffer(rhi::BufferHandle h, const rhi::BufferDesc &d,
                                        rhi::Access a) {
    if (!h)
        throw std::invalid_argument("Invalid imported buffer");
    auto r = createBuffer(d);
    auto &v = impl_->resources[r.index];
    v.buffer = h;
    v.imported = true;
    v.initial = a;
    return r;
}
uint32_t RenderGraph::addPass(std::string name, std::vector<GraphAccess> accesses,
                              GraphCallback callback, bool effect) {
    if (!callback)
        throw std::invalid_argument("Pass needs a callback");
    for (const auto &a : accesses) {
        auto i = impl_->index(a.resource);
        interval(impl_->resources[i], a.range);
        if (a.access == rhi::Access::None)
            throw std::invalid_argument("Pass access cannot be None");
    }
    auto i = static_cast<uint32_t>(impl_->passes.size());
    impl_->passes.push_back(
        {std::move(name), std::move(accesses), std::move(callback), effect, {}});
    return i;
}
void RenderGraph::dependsOn(uint32_t p, uint32_t d) {
    if (p >= impl_->passes.size() || d >= impl_->passes.size())
        throw std::invalid_argument("Invalid pass dependency");
    impl_->passes[p].dependencies.insert(d);
}
void RenderGraph::exportResource(GraphResource r) {
    impl_->exports.insert(impl_->index(r));
}
CompiledRenderGraph::CompiledRenderGraph() : impl_(std::make_unique<Impl>()) {}
CompiledRenderGraph::~CompiledRenderGraph() = default;
CompiledRenderGraph::CompiledRenderGraph(CompiledRenderGraph &&) noexcept = default;
CompiledRenderGraph &CompiledRenderGraph::operator=(CompiledRenderGraph &&) noexcept = default;
GraphStatistics CompiledRenderGraph::statistics() const {
    return impl_->stats;
}
std::vector<GraphPassInfo> CompiledRenderGraph::passes() const {
    std::vector<GraphPassInfo> result;
    for (size_t i = 0; i < impl_->passes.size(); ++i)
        result.push_back({impl_->passes[i].name, static_cast<uint32_t>(impl_->barriers[i].size())});
    return result;
}
CompiledRenderGraph RenderGraph::compile() const {
    CompiledRenderGraph graph;
    auto &out = *graph.impl_;
    out.owner = impl_->owner;
    out.resources = impl_->resources;
    auto passes = impl_->passes;
    struct Event {
        uint32_t pass;
        GraphAccess access;
    };
    // 阶段一：按声明顺序校验初始化范围，并为重叠 RAW/WAR/WAW 访问建立依赖。
    // 纹理使用 mip 区间，缓冲使用字节区间；不重叠范围不会产生数据依赖。
    std::vector<std::vector<Event>> history(out.resources.size());
    std::vector<std::vector<std::pair<size_t, size_t>>> initialized(out.resources.size());
    for (size_t r = 0; r < out.resources.size(); ++r)
        if (out.resources[r].imported && out.resources[r].initial != rhi::Access::None)
            initialized[r].push_back(interval(out.resources[r], {}));
    for (uint32_t p = 0; p < passes.size(); ++p) {
        auto &pass = passes[p];
        for (size_t a = 0; a < pass.accesses.size(); ++a)
            for (size_t b = a + 1; b < pass.accesses.size(); ++b) {
                const auto &x = pass.accesses[a];
                const auto &y = pass.accesses[b];
                if (x.resource == y.resource && (writes(x.access) || writes(y.access)) &&
                    overlaps(out.resources[x.resource.index], x.range, y.range))
                    throw std::invalid_argument("Overlapping accesses in " + pass.name +
                                                "; use one StorageReadWrite access");
            }
        for (const auto &access : pass.accesses) {
            auto r = access.resource.index;
            auto range = interval(out.resources[r], access.range);
            if (reads(access.access)) {
                auto spans = initialized[r];
                std::sort(spans.begin(), spans.end());
                size_t covered = range.first;
                for (auto [begin, end] : spans)
                    if (begin <= covered)
                        covered = std::max(covered, end);
                if (covered < range.second)
                    throw std::invalid_argument("Uninitialized read in pass " + pass.name);
            }
            for (const auto &old : history[r])
                if ((writes(access.access) || writes(old.access.access)) &&
                    overlaps(out.resources[r], access.range, old.access.range))
                    pass.dependencies.insert(old.pass);
            history[r].push_back({p, access});
            if (writes(access.access))
                initialized[r].push_back(range);
        }
    }
    // 阶段二：从导出资源的写入者和副作用 Pass 反向保留依赖闭包。
    std::vector<bool> live(passes.size());
    std::vector<uint32_t> pending;
    for (uint32_t p = 0; p < passes.size(); ++p)
        if (passes[p].sideEffect)
            pending.push_back(p);
    for (auto r : impl_->exports)
        for (const auto &event : history[r])
            if (writes(event.access.access))
                pending.push_back(event.pass);
    while (!pending.empty()) {
        auto p = pending.back();
        pending.pop_back();
        if (live[p])
            continue;
        live[p] = true;
        for (auto d : passes[p].dependencies)
            pending.push_back(d);
    }
    // 阶段三：稳定拓扑排序，优先选择声明较早的就绪 Pass，便于复现与调试。
    std::vector<uint32_t> order;
    std::set<uint32_t> ready;
    std::vector<size_t> degree(passes.size());
    for (uint32_t p = 0; p < passes.size(); ++p)
        if (live[p]) {
            for (auto d : passes[p].dependencies)
                if (live[d])
                    ++degree[p];
            if (!degree[p])
                ready.insert(p);
        }
    while (!ready.empty()) {
        auto p = *ready.begin();
        ready.erase(ready.begin());
        order.push_back(p);
        for (uint32_t n = 0; n < passes.size(); ++n)
            if (live[n] && passes[n].dependencies.contains(p) && --degree[n] == 0)
                ready.insert(n);
    }
    if (order.size() != static_cast<size_t>(std::count(live.begin(), live.end(), true)))
        throw std::invalid_argument("RenderGraph dependency cycle");
    // 阶段四：按最终执行顺序生成资源屏障，并记录每个逻辑资源的首末使用点。
    std::vector<std::vector<GraphAccess>> states(out.resources.size());
    for (uint32_t r = 0; r < out.resources.size(); ++r)
        if (out.resources[r].initial != rhi::Access::None)
            states[r].push_back({{r, 1, out.owner}, out.resources[r].initial, rhi::Stage::All, {}});
    const size_t unused = std::numeric_limits<size_t>::max();
    std::vector<size_t> first(out.resources.size(), unused), last(out.resources.size(), 0);
    for (size_t p = 0; p < order.size(); ++p) {
        const auto &pass = passes[order[p]];
        std::vector<std::pair<uint32_t, rhi::ResourceBarrier>> barriers;
        for (const auto &access : pass.accesses) {
            auto r = access.resource.index;
            first[r] = std::min(first[r], p);
            last[r] = p;
            for (const auto &old : states[r])
                if ((writes(old.access) || writes(access.access)) &&
                    overlaps(out.resources[r], old.range, access.range))
                    barriers.push_back({r,
                                        {old.access,
                                         access.access,
                                         out.resources[r].desc.index() == 0,
                                         {},
                                         {},
                                         old.stages,
                                         access.stages,
                                         access.range.firstMip,
                                         access.range.mipCount,
                                         access.range.offset,
                                         access.range.size}});
            states[r].push_back(access);
        }
        out.barriers.push_back(std::move(barriers));
        out.passes.push_back(pass);
    }
    for (auto r : impl_->exports)
        last[r] = order.size();
    // 阶段五：描述相同且生命周期严格不重叠的临时资源共享物理分配。
    // 导出资源的末次使用延长到图末尾，避免输出被其他逻辑资源覆盖。
    out.physical.assign(out.resources.size(), -1);
    std::vector<size_t> allocationLast;
    std::vector<uint32_t> resources(out.resources.size());
    std::iota(resources.begin(), resources.end(), 0);
    std::sort(resources.begin(), resources.end(),
              [&](auto a, auto b) { return first[a] < first[b]; });
    for (auto r : resources)
        if (!out.resources[r].imported && first[r] != unused) {
            size_t a = 0;
            for (; a < out.allocations.size(); ++a)
                if (allocationLast[a] < first[r] && same(out.resources[r], out.allocations[a]))
                    break;
            if (a == out.allocations.size()) {
                out.allocations.push_back(out.resources[r]);
                allocationLast.push_back(last[r]);
            } else {
                allocationLast[a] = last[r];
                // 即使逻辑句柄不同，复用同一存储前仍需等待旧用途的访问结束。
                out.barriers[first[r]].push_back({r,
                                                  {rhi::Access::StorageReadWrite, rhi::Access::None,
                                                   out.resources[r].desc.index() == 0}});
            }
            out.physical[r] = static_cast<int>(a);
        }
    out.stats = {static_cast<uint32_t>(passes.size()), static_cast<uint32_t>(order.size()),
                 static_cast<uint32_t>(passes.size() - order.size()),
                 static_cast<uint32_t>(out.resources.size()),
                 static_cast<uint32_t>(out.allocations.size())};
    return graph;
}
rhi::CompletionToken CompiledRenderGraph::execute(rhi::Device &device,
                                                  rhi::QueryHandle timestamps) {
    if (impl_->device && impl_->device != detail::RhiAccess::share(device))
        throw std::invalid_argument("Compiled graph cannot switch devices");
    if (!impl_->resolved) {
        // 保留设备后端而不是 Device 包装对象地址，使包装对象移动后仍可执行/清理。
        impl_->device = detail::RhiAccess::share(device);
        auto values = std::make_unique<GraphResources::Impl>();
        values->owner = impl_->owner;
        values->textures.resize(impl_->resources.size());
        values->buffers.resize(impl_->resources.size());
        for (auto &a : impl_->allocations) {
            if (const auto *d = std::get_if<rhi::TextureDesc>(&a.desc)) {
                if (!a.texture)
                    a.texture = device.createTexture(*d);
            } else if (!a.buffer)
                a.buffer = device.createBuffer(std::get<rhi::BufferDesc>(a.desc));
        }
        for (size_t r = 0; r < impl_->resources.size(); ++r) {
            const auto &resource = impl_->physical[r] >= 0
                                       ? impl_->allocations[static_cast<size_t>(impl_->physical[r])]
                                       : impl_->resources[r];
            values->textures[r] = resource.texture;
            values->buffers[r] = resource.buffer;
        }
        impl_->resolved = std::unique_ptr<GraphResources>(new GraphResources(std::move(values)));
    }
    rhi::CommandEncoder encoder;
    for (uint32_t p = 0; p < impl_->passes.size(); ++p) {
        // 回调只能取到当前声明的资源；图不会分析回调内部的任意外部资源访问。
        impl_->resolved->impl_->allowed.clear();
        for (const auto &access : impl_->passes[p].accesses)
            impl_->resolved->impl_->allowed.insert(access.resource.index);
        if (!impl_->barriers[p].empty()) {
            std::vector<rhi::ResourceBarrier> barriers;
            for (auto [r, barrier] : impl_->barriers[p]) {
                if (barrier.texture)
                    barrier.image = impl_->resolved->texture({r, 1, impl_->owner});
                else
                    barrier.buffer = impl_->resolved->buffer({r, 1, impl_->owner});
                barriers.push_back(barrier);
            }
            encoder.barrier(barriers);
        }
        if (timestamps)
            encoder.timestamp(timestamps, p * 2);
        impl_->passes[p].callback(encoder, *impl_->resolved);
        if (timestamps)
            encoder.timestamp(timestamps, p * 2 + 1);
    }
    return device.submit(encoder.finish());
}
} // namespace renderer
