#include "internal/rhi_commands.hpp"
#include "renderer/rhi.hpp"
#include <limits>
#include <stdexcept>

namespace renderer::rhi {
CommandList::CommandList() = default;
CommandList::~CommandList() = default;
CommandList::CommandList(const CommandList &) = default;
CommandList &CommandList::operator=(const CommandList &) = default;
CommandList::CommandList(CommandList &&) noexcept = default;
CommandList &CommandList::operator=(CommandList &&) noexcept = default;
struct CommandEncoder::Impl {
    std::vector<detail::Command> commands;
    bool rendering = false, finished = false;
    void check() const {
        if (finished)
            throw std::logic_error("Encoder is already finished");
    }
    template <class T> void add(T value) {
        check();
        commands.emplace_back(std::move(value));
    }
    void requireRendering(bool required) const {
        check();
        if (rendering != required)
            throw std::logic_error(required ? "Draw requires a rendering pass"
                                            : "Operation must be outside a rendering pass");
    }
};
CommandEncoder::CommandEncoder() : impl_(std::make_unique<Impl>()) {}
CommandEncoder::~CommandEncoder() = default;
CommandEncoder::CommandEncoder(CommandEncoder &&) noexcept = default;
CommandEncoder &CommandEncoder::operator=(CommandEncoder &&) noexcept = default;
void CommandEncoder::beginRendering(const RenderingDesc &d) {
    impl_->requireRendering(false);
    if (d.colors.empty() && !d.depth)
        throw std::invalid_argument("Pass needs attachments");
    impl_->add(detail::Begin{d});
    impl_->rendering = true;
}
void CommandEncoder::endRendering() {
    impl_->requireRendering(true);
    impl_->add(detail::End{});
    impl_->rendering = false;
}
void CommandEncoder::bindPipeline(PipelineHandle h) {
    impl_->add(detail::Pipeline{h});
}
void CommandEncoder::bindGroup(BindingGroupHandle h) {
    impl_->add(detail::Group{h});
}
void CommandEncoder::bindVertexBuffer(uint32_t b, BufferHandle h, size_t offset) {
    impl_->add(detail::VertexBuffer{b, h, offset});
}
void CommandEncoder::bindIndexBuffer(BufferHandle h, size_t offset) {
    impl_->add(detail::IndexBuffer{h, offset});
}
void CommandEncoder::viewport(uint32_t w, uint32_t h) {
    impl_->add(detail::Viewport{w, h});
}
void CommandEncoder::scissor(int x, int y, uint32_t w, uint32_t h) {
    impl_->add(detail::Scissor{x, y, w, h});
}
void CommandEncoder::draw(uint32_t c, uint32_t i, uint32_t f, uint32_t base) {
    impl_->requireRendering(true);
    constexpr uint32_t limit = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
    if (c > limit || i > limit || f > limit - c || base > limit - i)
        throw std::invalid_argument("Draw range exceeds the portable integer limit");
    impl_->add(detail::Draw{c, i, f, base});
}
void CommandEncoder::drawIndexed(uint32_t c, uint32_t i, uint32_t f, int32_t base,
                                 uint32_t instance) {
    impl_->requireRendering(true);
    constexpr uint32_t limit = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
    if (c > limit || i > limit || instance > limit - i)
        throw std::invalid_argument("Indexed draw range exceeds the portable integer limit");
    impl_->add(detail::DrawIndexed{c, i, f, base, instance});
}
void CommandEncoder::drawIndexedIndirect(BufferHandle h, size_t o, uint32_t n, uint32_t stride) {
    impl_->requireRendering(true);
    if (stride < 20 || stride % 4 || o % 4 ||
        stride > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) ||
        n > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
        throw std::invalid_argument("Invalid indirect draw alignment");
    impl_->add(detail::Indirect{h, o, n, stride, false});
}
void CommandEncoder::dispatch(uint32_t x, uint32_t y, uint32_t z) {
    impl_->requireRendering(false);
    impl_->add(detail::Dispatch{x, y, z});
}
void CommandEncoder::dispatchIndirect(BufferHandle h, size_t offset) {
    impl_->requireRendering(false);
    if (offset % 4)
        throw std::invalid_argument("Invalid dispatch offset");
    impl_->add(detail::Indirect{h, offset, 1, 12, true});
}
void CommandEncoder::copyBuffer(BufferHandle s, BufferHandle d, size_t n, size_t so, size_t doff) {
    impl_->requireRendering(false);
    impl_->add(detail::Copy{s, d, n, so, doff});
}
void CommandEncoder::barrier(std::span<const ResourceBarrier> b) {
    impl_->requireRendering(false);
    impl_->add(detail::Barriers{{b.begin(), b.end()}});
}
void CommandEncoder::copyTexture(TextureHandle source, TextureHandle destination,
                                 const TextureCopy &region) {
    impl_->requireRendering(false);
    if (!region.width || !region.height)
        throw std::invalid_argument("Texture copy dimensions must be positive");
    impl_->add(detail::CopyTexture{source, destination, region});
}
void CommandEncoder::timestamp(QueryHandle q, uint32_t i) {
    impl_->add(detail::Timestamp{q, i});
}
void CommandEncoder::present(TextureHandle t) {
    impl_->requireRendering(false);
    impl_->add(detail::Present{t});
}
CommandList CommandEncoder::finish() {
    impl_->requireRendering(false);
    auto data = std::make_shared<CommandList::Impl>();
    data->commands = std::move(impl_->commands);
    impl_->finished = true;
    CommandList result;
    result.impl_ = std::move(data);
    return result;
}
} // namespace renderer::rhi
