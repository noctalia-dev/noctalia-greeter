#pragma once

#include <cmath>
#include <cstdint>
#include <functional>

enum class Easing {
  Linear,
  EaseInCubic,
  EaseOutCubic,
  EaseInOutCubic,
  EaseOutBack,
};

inline float easeValue(Easing easing, float t) {
  switch (easing) {
  case Easing::Linear:
    return t;
  case Easing::EaseInCubic:
    return t * t * t;
  case Easing::EaseOutCubic:
    return 1.0f - static_cast<float>(std::pow(1.0f - t, 3));
  case Easing::EaseInOutCubic:
    return t < 0.5f ? 4 * t * t * t : 1 - static_cast<float>(std::pow(-2 * t + 2, 3)) * 0.5f;
  case Easing::EaseOutBack: {
    float c1 = 1.70158f;
    float c3 = c1 + 1.0f;
    return 1 + c3 * static_cast<float>(std::pow(t - 1, 3)) + c1 * static_cast<float>(std::pow(t - 1, 2));
  }
  }
  return t;
}

struct Animation {
  std::uint64_t id = 0;
  float from = 0.0f;
  float to = 0.0f;
  float durationMs = 0.0f;
  float elapsedMs = 0.0f;
  Easing easing = Easing::Linear;
  std::function<void(float)> onUpdate;
  std::function<void()> onComplete;
  void* owner = nullptr;
  bool finished = false;

  bool tick(float deltaMs) {
    if (finished)
      return true;
    elapsedMs += deltaMs;
    float t = std::min(elapsedMs / durationMs, 1.0f);
    float eased = easeValue(easing, t);
    float value = from + (to - from) * eased;
    if (onUpdate)
      onUpdate(value);
    if (t >= 1.0f) {
      finished = true;
      if (onComplete)
        onComplete();
      return true;
    }
    return false;
  }
};
