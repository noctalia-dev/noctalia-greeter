#pragma once
#include "render/scene/node.h"
class Separator : public Node {
public:
  Separator() : Node(NodeType::Container) { setSize(1, 1); }
  void setOrientation(bool vertical) {
    if (vertical)
      setSize(1, 1);
    else
      setSize(1, 1);
  }

private:
  LayoutSize doMeasure(Renderer&, const LayoutConstraints& c) override { return c.constrain({1, 1}); }
  void doLayout(Renderer&) override {}
};
