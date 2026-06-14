#pragma once
#include "render/scene/node.h"

#include <cstdint>
class GraphNode : public Node {
public:
  GraphNode() : Node(NodeType::Base) {}

private:
  LayoutSize doMeasure(Renderer&, const LayoutConstraints& c) override { return c.constrain({width(), height()}); }
  void doLayout(Renderer&) override {}
};
