#pragma once

#include "greeter/widgets/widget_catalog.h"

#include <functional>

class Node;

namespace greeter {

  // The view owns menu anchors, capability checks and execution policy.
  // The view must outlive the hosts that retain these callbacks.
  struct WidgetActions {
    std::function<bool(WidgetAction)> available;
    std::function<void(WidgetAction, Node& source)> activate;
  };

} // namespace greeter
