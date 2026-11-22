#include "greeter/widgets/label_widget.h"

#include <label_widget.h>
#include <memory>
#include <utility>

namespace greeter {

  LabelWidget::LabelWidget(const Options& options) {
    setHitTestVisible(false);
    auto label = std::make_unique<::LabelWidget>();
    m_label = label.get();
    label->setTitle(options.title);
    label->setDescription(options.description);
    label->setFontFamily(options.fontFamily);
    label->setColor(options.color);
    label->setOpacity(options.opacity);
    label->setShadow(options.shadow);
    addChild(std::move(label));
  }

  void LabelWidget::layoutAtScale(Renderer& renderer, float scale) {
    m_label->setContentScale(scale);
    m_label->layout(renderer);
    setSize(m_label->width(), m_label->height());
  }

} // namespace greeter
