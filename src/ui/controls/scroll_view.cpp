#include "ui/controls/scroll_view.h"
ScrollView::ScrollView() : Node(NodeType::Container) {}
LayoutSize ScrollView::doMeasure(Renderer&, const LayoutConstraints& c) { return c.constrain({width(), height()}); }
void ScrollView::doLayout(Renderer& r) {
  for (auto& ch : m_children)
    if (ch->visible())
      ch->layout(r);
}
