#pragma once
#include "render/scene/node.h"
class Spinner : public Node {
public:
  Spinner() : Node(NodeType::Container) {}
  void setAnimating(bool) {}

private:
  LayoutSize doMeasure(Renderer&, const LayoutConstraints& c) override { return c.constrain({24, 24}); }
  void doLayout(Renderer&) override {}
};
