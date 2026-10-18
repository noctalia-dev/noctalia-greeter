#pragma once

struct FrameDemand {
  bool redrawRequested = false;
  bool layoutRequested = false;
  bool sceneLayoutDirty = false;
  bool scenePaintDirty = false;
  bool animating = false;
};

[[nodiscard]] bool frameNeedsRedraw(const FrameDemand& demand) noexcept;
