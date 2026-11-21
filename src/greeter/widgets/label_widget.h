#pragma once

#include "greeter/widgets/widget_content.h"
#include "ui/palette.h"

#include <string>

class Label;

namespace greeter {

  class LabelWidget final : public WidgetContent {
  public:
    struct Options {
      std::string title;
      std::string description;
      std::string fontFamily;
      ColorSpec color;
      float opacity;
      bool shadow;
    };

    explicit LabelWidget(const Options& options);
    void layoutAtScale(Renderer& renderer, float scale) override;

  private:
    Label* m_title = nullptr;
    Label* m_description = nullptr;
    float m_opacity;
    bool m_shadow;
  };

} // namespace greeter
