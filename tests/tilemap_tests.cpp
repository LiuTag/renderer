/** @file @brief 负坐标分块、脏块更新、瓦片尺寸、卸载和快照存活测试。 */
#include "sprite_fixture.hpp"
#include <renderer/tilemap/tilemap.hpp>
using namespace sprite_test;
int main() {
    try {
        Fixture f;
        TileMap map({.tileSize = 2, .chunkSize = 4});
        map.set(-1, -1, f.first);
        map.set(0, 0, f.second);
        require(map.tileCount() == 2 && map.chunkCount() == 2);
        require(map.get(-1, -1) == f.first);
        auto batches = map.batches(f.assets);
        auto old = f.extractor.extract(f.scene, f.assets, batches);
        require(old.spriteCount() == 2);
        map.unloadChunk(-1, -1);
        require(map.tileCount() == 1);
        map.set(0, 0, {});
        require(map.chunkCount() == 0);
        require(old.spriteCount() == 2);
        std::vector<TileChunkCell> cells{{0, 0, f.first}};
        map.replaceChunk(1, 1, f.assets, cells);
        require(map.get(4, 4) == f.first && map.tileCount() == 1);
        cells.push_back(cells[0]);
        throws([&] { map.replaceChunk(1, 1, f.assets, cells); });
        require(map.get(4, 4) == f.first && map.tileCount() == 1);
        cells.resize(1);
        cells[0].sprite = {};
        throws([&] { map.replaceChunk(1, 1, f.assets, cells); });
        require(map.get(4, 4) == f.first);
        throws([] { TileMap bad({.tileSize = 0}); });
        std::cout << "tilemap tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
