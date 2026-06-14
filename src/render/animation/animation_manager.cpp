#include "render/animation/animation_manager.h"

#include <algorithm>

std::uint64_t AnimationManager::animate(
    float from, float to, float durationMs, Easing easing, std::function<void(float)> onUpdate,
    std::function<void()> onComplete, void* owner
) {
  Animation anim;
  anim.id = m_nextId++;
  anim.from = from;
  anim.to = to;
  anim.durationMs = durationMs;
  anim.easing = easing;
  anim.onUpdate = std::move(onUpdate);
  anim.onComplete = std::move(onComplete);
  anim.owner = owner;
  m_animations.push_back(std::move(anim));
  return m_animations.back().id;
}

void AnimationManager::cancel(std::uint64_t id) {
  m_animations.erase(
      std::remove_if(m_animations.begin(), m_animations.end(), [id](const Animation& a) { return a.id == id; }),
      m_animations.end()
  );
}

void AnimationManager::cancelForOwner(void* owner) {
  m_animations.erase(
      std::remove_if(
          m_animations.begin(), m_animations.end(), [owner](const Animation& a) { return a.owner == owner; }
      ),
      m_animations.end()
  );
}

void AnimationManager::tick(float deltaMs) {
  for (auto& anim : m_animations) {
    anim.tick(deltaMs);
  }
  m_animations.erase(
      std::remove_if(m_animations.begin(), m_animations.end(), [](const Animation& a) { return a.finished; }),
      m_animations.end()
  );
}
