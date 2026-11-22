#pragma once

#include "greeter/widgets/widget_content.h"
#include "ui/controls/button.h"

#include <optional>
#include <string>

class DesktopButton;

namespace greeter {

  class ButtonWidget final : public WidgetContent {
  public:
    struct Options {
      std::string glyph;
      std::string label;
      std::string fontFamily;
      ButtonVariant variant;
      bool background;
      std::optional<ColorSpec> color;
      ColorSpec hoverBackground;
    };
    explicit ButtonWidget(const Options& options);
    void layoutAtScale(Renderer& renderer, float scale) override;
    bool layoutInBox(Renderer& renderer, float width, float height) override;

  private:
    void layoutButton(Renderer& renderer, float scale, float width = 0, float height = 0);

    ::DesktopButton* m_button = nullptr;
  };

} // namespace greeter
