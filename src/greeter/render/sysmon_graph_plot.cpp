#include "greeter/render/sysmon_graph_plot.h"

#include "greeter/render/sysmon_graph_program.h"
#include "render/backend/render_backend.h"
#include "render/core/renderer.h"
#include "render/core/texture_manager.h"

#include <algorithm>
#include <utility>

namespace greeter {

  SysmonGraphPlot::SysmonGraphPlot(ColorSpec primary, ColorSpec secondary)
      : m_primary(primary), m_secondary(secondary) {
    setHitTestVisible(false);
    const auto applyPalette = [this] {
      m_style.lineColor1 = resolveColorSpec(m_primary);
      m_style.lineColor2 = resolveColorSpec(m_secondary);
      markPaintDirty();
    };
    m_paletteConnection = paletteChanged().connect(applyPalette);
    applyPalette();
  }

  SysmonGraphPlot::~SysmonGraphPlot() {
    if (m_textures != nullptr)
      m_textures->unload(m_texture);
  }

  void SysmonGraphPlot::setSeries(std::vector<float> primary, std::vector<float> secondary) {
    if (m_series[0] == primary && m_series[1] == secondary)
      return;
    m_series = {std::move(primary), std::move(secondary)};
    m_dataDirty = true;
    markPaintDirty();
  }

  void SysmonGraphPlot::sync(Renderer& renderer, float scale) {
    m_style.lineWidth = 0.75F * scale;
    if (m_dataDirty || m_textures != &renderer.textureManager())
      upload(renderer);
  }

  void SysmonGraphPlot::upload(Renderer& renderer) {
    if (m_textures != nullptr && m_textures != &renderer.textureManager())
      m_textures->unload(m_texture);
    m_textures = &renderer.textureManager();
    m_textureWidth = static_cast<int>(std::max({m_series[0].size() + 2, m_series[1].size() + 2, std::size_t{4}}));
    m_pixels.assign(static_cast<std::size_t>(m_textureWidth) * 4, 0);
    for (std::size_t series = 0; series < m_series.size(); ++series) {
      const auto& values = m_series[series];
      for (std::size_t i = 0; i < static_cast<std::size_t>(m_textureWidth); ++i) {
        float value = 0.0F;
        if (i < values.size()) {
          value = values[i];
        } else if (!values.empty()) {
          value = values.back();
          if (i == values.size() && values.size() >= 2)
            value = std::clamp(value + (value - values[values.size() - 2]) * 0.5F, 0.0F, 1.0F);
        }
        m_pixels[i * 4 + series] = static_cast<std::uint8_t>(value * 255.0F);
      }
    }
    m_style.count1 = static_cast<float>(m_series[0].size());
    m_style.count2 = static_cast<float>(m_series[1].size());
    m_dataDirty = !m_textures->replace(
        m_texture, m_pixels.data(), m_textureWidth, 1, TextureDataFormat::Rgba, TextureFilter::Nearest
    );
  }

  void SysmonGraphPlot::draw(RenderBackend& backend, const CustomDrawContext& context) const {
    if (!m_texture.valid() || (m_style.count1 < 4 && m_style.count2 < 4))
      return;
    auto style = m_style;
    style.lineColor1.a *= context.opacity;
    style.lineColor2.a *= context.opacity;
    auto& program = backend.extension<SysmonGraphProgram>();
    program.ensureInitialized();
    program.draw(
        m_texture.id, m_textureWidth, context.surfaceWidth, context.surfaceHeight, context.width, context.height, style,
        context.transform
    );
  }

  void SysmonGraphPlot::doInvalidateGpuResources(Renderer& renderer) {
    if (m_textures != nullptr)
      m_textures->unload(m_texture);
    m_textures = nullptr;
    upload(renderer);
  }

} // namespace greeter
