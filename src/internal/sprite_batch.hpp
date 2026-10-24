/** @file @brief 批量精灵的不可变解析数据与核心访问桥，不导出到 SDK。 */
#ifndef RENDERER_SRC_INTERNAL_SPRITE_BATCH_HPP_INCLUDED
#define RENDERER_SRC_INTERNAL_SPRITE_BATCH_HPP_INCLUDED
#include "renderer/sprite_batch.hpp"
#include "sprite.hpp"
namespace renderer::detail {
struct SpriteBatchData {
    std::vector<SpriteRecord> sprites;
    AABB bounds;
};
struct SpriteBatchAccess {
    static std::shared_ptr<const SpriteBatchData> data(const SpriteBatch &batch);
};
} // namespace renderer::detail
#endif
