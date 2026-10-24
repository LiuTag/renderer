#include "renderer/rhi.hpp"
#include "internal/rhi_backend.hpp"
#include <stdexcept>
namespace renderer::rhi {
Device::Device(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Device::~Device() = default;
Device::Device(Device &&) noexcept = default;
Device &Device::operator=(Device &&) noexcept = default;
DeviceCaps Device::capabilities() const {
    return impl_->backend->capabilities();
}
FormatFeatures Device::formatFeatures(Format f) const {
    return impl_->backend->formatFeatures(f);
}
BufferHandle Device::createBuffer(const BufferDesc &d, std::span<const std::byte> b) {
    return impl_->backend->createBuffer(d, b);
}
TextureHandle Device::createTexture(const TextureDesc &d) {
    return impl_->backend->createTexture(d);
}
TextureHandle Device::createTextureView(TextureHandle h, uint32_t f, uint32_t c) {
    return impl_->backend->createTextureView(h, f, c);
}
SamplerHandle Device::createSampler(const SamplerDesc &d) {
    return impl_->backend->createSampler(d);
}
PipelineHandle Device::createGraphicsPipeline(const GraphicsPipelineDesc &d) {
    return impl_->backend->createGraphicsPipeline(d);
}
PipelineHandle Device::createComputePipeline(const ShaderArtifact &d, const std::string &n) {
    return impl_->backend->createComputePipeline(d, n);
}
BindingGroupHandle Device::createBindingGroup(const BindingGroupDesc &d) {
    return impl_->backend->createBindingGroup(d);
}
QueryHandle Device::createTimestampQueries(uint32_t n) {
    return impl_->backend->createTimestampQueries(n);
}
void Device::destroy(BufferHandle h) {
    impl_->backend->destroy(h);
}
void Device::destroy(TextureHandle h) {
    impl_->backend->destroy(h);
}
void Device::destroy(SamplerHandle h) {
    impl_->backend->destroy(h);
}
void Device::destroy(PipelineHandle h) {
    impl_->backend->destroy(h);
}
void Device::destroy(BindingGroupHandle h) {
    impl_->backend->destroy(h);
}
void Device::destroy(QueryHandle h) {
    impl_->backend->destroy(h);
}
void *Device::map(BufferHandle h) {
    return impl_->backend->map(h);
}
void Device::upload(BufferHandle h, size_t o, std::span<const std::byte> d) {
    impl_->backend->upload(h, o, d);
}
void Device::upload(TextureHandle h, std::span<const uint8_t> d, uint32_t m) {
    impl_->backend->upload(h, d, m);
}
void Device::generateMipmaps(TextureHandle h) {
    impl_->backend->generateMipmaps(h);
}
std::vector<std::byte> Device::readBuffer(BufferHandle h, size_t n, size_t o) {
    return impl_->backend->readBuffer(h, n, o);
}
RgbaImage Device::readTexture(TextureHandle h) {
    return impl_->backend->readTexture(h);
}
CompletionToken Device::submit(const CommandList &l) {
    if (!l.impl_)
        throw std::invalid_argument("Empty command list");
    return impl_->backend->submit(l.impl_->commands);
}
bool Device::complete(CompletionToken t) {
    return impl_->backend->complete(t);
}
void Device::wait(CompletionToken t) {
    impl_->backend->wait(t);
}
void Device::waitIdle() {
    impl_->backend->waitIdle();
}
std::optional<uint64_t> Device::timestampResult(QueryHandle q, uint32_t i, bool w) {
    return impl_->backend->timestampResult(q, i, w);
}
size_t Device::pendingDeletions() const {
    return impl_->backend->pendingDeletions();
}
uint32_t Device::debugErrorCount() const {
    return impl_->backend->debugErrorCount();
}
} // namespace renderer::rhi
