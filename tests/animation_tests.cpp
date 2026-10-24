#include "renderer/animation/sprite_animation.hpp"
#include "sprite_fixture.hpp"
using namespace sprite_test;
int main() {
    const std::vector<std::pair<const char *, std::function<void()>>> tests = {
        {"direction animation and crossing events",
         [] {
             Fixture f;
             const auto n = f.add();
             SpriteAnimationClipDesc d;
             for (int i = 0; i < 4; ++i)
                 d.directions.push_back({{f.first, .125, "step"}, {f.second, .125, "hit"}});
             SpriteAnimationClip clip(d);
             SpriteAnimator player;
             player.play(clip);
             require(player.takeEvents().size() == 1);
             player.advance(.625);
             require(player.frameIndex() == 1);
             auto events = player.takeEvents();
             require(events.size() == 5 && events[0].name == "hit" && events[1].cycle == 1 &&
                     events.back().cycle == 2);
             player.selectDirection({1, 0, 0}, Mat4::identity());
             require(player.directionIndex() == 1);
             player.selectDirection({1, 0, 0}, Mat4::rotationY(radians(90)));
             require(player.directionIndex() == 0);
             player.apply(f.scene, n);
             require(f.scene.component<SpriteRenderer>(n)->sprite == f.second);
             player.setPaused(true);
             player.advance(1);
             require(player.frameIndex() == 1);
             player.play(clip, false);
             player.advance(1);
             require(player.finished() && player.frameIndex() == 1);
             player.play(clip);
             player.takeEvents();
             throws([&] { player.advance(1000); });
             require(player.frameIndex() == 0 && player.takeEvents().empty());
             throws([&] { player.advance(-1); });
             throws([&] { player.setSpeed(-1); });
             auto bad = d;
             bad.directions[1][0].duration = .2;
             throws([&] { SpriteAnimationClip invalid(bad); });
         }},
        {"eight directions, speed and event-free catch-up",
         [] {
             Fixture f;
             SpriteAnimationClipDesc d;
             for (int i = 0; i < 8; ++i)
                 d.directions.push_back({{f.first, .125, {}}, {f.second, .125, {}}});
             SpriteAnimationClip clip(d);
             SpriteAnimator animator;
             animator.play(clip);
             for (int i = 0; i < 8; ++i) {
                 const float angle = radians(float(i) * 45);
                 animator.selectDirection({std::sin(angle), 0, std::cos(angle)}, Mat4::identity());
                 require(animator.directionIndex() == size_t(i));
             }
             animator.setSpeed(2);
             animator.advance(.0625);
             require(animator.frameIndex() == 1);
             animator.advance(1000000);
             require(animator.frameIndex() == 1 && animator.takeEvents().empty());
             animator.setSpeed(0);
             animator.advance(100);
             require(animator.frameIndex() == 1);
         }},
    };
    size_t passed = 0;
    for (const auto &[name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception &e) {
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
            return 1;
        }
    }
    std::cout << passed << " animation CPU tests passed\n";
}
