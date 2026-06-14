#pragma once
#include "render/scene/node.h"
class ScrollView : public Node {
public:
  ScrollView();

private:
  LayoutSize doMeasure(Renderer& renderer, const LayoutConstraints& constraints) override;
  void doLayout(Renderer& renderer) override;
};
