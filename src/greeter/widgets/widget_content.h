#pragma once

#include "render/scene/node.h"

namespace greeter {

  // Content measures at the host's fitted scale; geometry changes happen before rendering.
  class WidgetContent : public Node {
  public:
    virtual void layoutAtScale(Renderer& renderer, float scale) = 0;
  };

} // namespace greeter
