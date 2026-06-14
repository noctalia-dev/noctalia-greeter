#pragma once

class MotionService {
public:
  static MotionService& instance();
  void setEnabled(bool enabled) { m_enabled = enabled; }
  void setSpeed(float speed) { m_speed = speed; }
  [[nodiscard]] bool enabled() const noexcept { return m_enabled; }
  [[nodiscard]] float speed() const noexcept { return m_speed; }

private:
  bool m_enabled = true;
  float m_speed = 1.0f;
};
