#pragma once

#include "render/animation/animation.h"

#include <cstdint>
#include <functional>
#include <vector>

class AnimationManager {
public:
  std::uint64_t animate(
      float from, float to, float durationMs, Easing easing, std::function<void(float)> onUpdate,
      std::function<void()> onComplete = nullptr, void* owner = nullptr
  );
  void cancel(std::uint64_t id);
  void cancelForOwner(void* owner);
  void tick(float deltaMs);

private:
  std::vector<Animation> m_animations;
  std::uint64_t m_nextId = 1;
};
