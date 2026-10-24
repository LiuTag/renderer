/** @file @brief 确定性固定步、容量、寿命、停止发射和批次保留测试。 */
#include "sprite_fixture.hpp"
#include <renderer/particles/particles.hpp>
using namespace sprite_test;
int main() {
    try {
        Fixture f;
        ParticleEmitterDesc d;
        d.sprite = f.first;
        d.rate = 60;
        d.capacity = 10;
        d.minLifetime = d.maxLifetime = .1f;
        ParticleSystem a(d), b(d);
        a.advance(1.0 / 30);
        b.advance(1.0 / 60);
        b.advance(1.0 / 60);
        require(a.particles().size() == b.particles().size() && a.particles().size() == 2);
        near(a.particles()[0].position.x, b.particles()[0].position.x);
        a.burst(100);
        require(a.particles().size() == 10);
        auto batch = a.batch(f.assets);
        a.clear();
        require(batch.size() == 10 && a.particles().empty());
        b.setEmitting(false);
        b.advance(1);
        require(b.particles().empty());
        throws([&] { a.advance(-1); });
        throws([&] { a.setRate(-1); });
        for (auto preset : {ParticlePreset::Fire, ParticlePreset::Smoke, ParticlePreset::Rain,
                            ParticlePreset::Snow}) {
            ParticleSystem p(particlePreset(preset, f.first));
            p.advance(.1);
            require(!p.particles().empty());
        }
        std::cout << "particle tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
