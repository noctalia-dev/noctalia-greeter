#include "greeter/frame_demand.h"
#include "render/animation/animation_manager.h"

#include <cstdio>
#include <string_view>

namespace {

  bool expect(std::string_view name, bool actual, bool expected) {
    if (actual == expected) {
      return true;
    }
    std::fprintf(
        stderr, "%.*s: expected %s, got %s\n", static_cast<int>(name.size()), name.data(), expected ? "true" : "false",
        actual ? "true" : "false"
    );
    return false;
  }

  bool redrawWhileAnimating(const AnimationManager& animations) {
    return frameNeedsRedraw(FrameDemand{.animating = animations.hasActive()});
  }

} // namespace

int main() {
  bool passed = true;
  passed = expect("idle frame needs no redraw", frameNeedsRedraw(FrameDemand{}), false) && passed;
  passed =
      expect("requested redraw needs redraw", frameNeedsRedraw(FrameDemand{.redrawRequested = true}), true) && passed;
  passed =
      expect("requested layout needs redraw", frameNeedsRedraw(FrameDemand{.layoutRequested = true}), true) && passed;
  passed = expect("dirty scene layout needs redraw", frameNeedsRedraw(FrameDemand{.sceneLayoutDirty = true}), true)
      && passed;
  passed =
      expect("dirty scene paint needs redraw", frameNeedsRedraw(FrameDemand{.scenePaintDirty = true}), true) && passed;

  AnimationManager animations;
  int owner = 0;
  animations.animate(0.0f, 1.0f, 100.0f, Easing::Linear, [](float) {}, nullptr, &owner);
  passed = expect("running animation needs redraw", redrawWhileAnimating(animations), true) && passed;
  animations.tick(50.0f);
  passed = expect("half-finished animation needs redraw", redrawWhileAnimating(animations), true) && passed;
  animations.tick(50.0f);
  passed = expect("finished animation needs no redraw", redrawWhileAnimating(animations), false) && passed;

  animations.animate(0.0f, 1.0f, 100.0f, Easing::Linear, [](float) {}, nullptr, &owner);
  animations.cancelForOwner(&owner);
  passed = expect("cancelled animation needs no redraw", redrawWhileAnimating(animations), false) && passed;
  return passed ? 0 : 1;
}
