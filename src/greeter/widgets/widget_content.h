#pragma once

#include "render/scene/node.h"

#include <chrono>
#include <optional>

namespace greeter {

  class SysmonSnapshot;

  // Content measures at the host's fitted scale; geometry changes happen before rendering.
  class WidgetContent : public Node {
  public:
    virtual void layoutAtScale(Renderer& renderer, float scale) = 0;
    virtual bool alignsToStart() const noexcept { return false; }
    virtual std::optional<std::chrono::seconds> tickInterval() const { return std::nullopt; }
    virtual NodeInvalidation timeInvalidation() const noexcept { return NodeInvalidation::Layout; }
    virtual bool updateMetrics(const SysmonSnapshot& snapshot) {
      (void)snapshot;
      return false;
    }
    virtual bool updateTime(std::chrono::sys_seconds now) {
      (void)now;
      return false;
    }
  };

} // namespace greeter
