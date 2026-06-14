#pragma once
#include "render/scene/node.h"
class Spacer : public Node {
public:
  Spacer() : Node(NodeType::Container) { setParticipatesInLayout(true); }
  void setWidth(float w) { setSize(w, height()); }
  void setHeight(float h) { setSize(width(), h); }

private:
  LayoutSize doMeasure(Renderer&, const LayoutConstraints& c) override { return c.constrain({width(), height()}); }
  void doLayout(Renderer&) override {}
};
