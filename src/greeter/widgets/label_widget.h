#pragma once

#include "greeter/widgets/widget_content.h"
#include "ui/palette.h"

#include <string>

class LabelWidget;

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
    ::LabelWidget* m_label = nullptr;
  };

} // namespace greeter
