#pragma once
#include "render/scene/node.h"
class Image : public Node {
public:
  Image();
  void setImagePath(const std::string& path);
  void setFillMode(const std::string& mode);

private:
  LayoutSize doMeasure(Renderer& renderer, const LayoutConstraints& constraints) override;
  void doLayout(Renderer& renderer) override;
  std::string m_path;
};
