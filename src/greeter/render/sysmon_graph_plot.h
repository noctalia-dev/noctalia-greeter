#pragma once

#include "greeter/render/sysmon_graph_types.h"
#include "render/core/texture_handle.h"
#include "render/scene/custom_draw_node.h"
#include "ui/palette.h"

#include <array>
#include <vector>

class TextureManager;

namespace greeter {

  class SysmonGraphPlot final : public CustomDrawNode {
  public:
    SysmonGraphPlot(ColorSpec primary, ColorSpec secondary);
    ~SysmonGraphPlot() override;
    void setSeries(std::vector<float> primary, std::vector<float> secondary);
    void sync(Renderer& renderer, float scale);
    void draw(RenderBackend& backend, const CustomDrawContext& context) const override;

  private:
    void doInvalidateGpuResources(Renderer& renderer) override;
    void upload(Renderer& renderer);
    SysmonGraphStyle m_style;
    ColorSpec m_primary;
    ColorSpec m_secondary;
    Signal<>::ScopedConnection m_paletteConnection;
    std::array<std::vector<float>, 2> m_series;
    std::vector<std::uint8_t> m_pixels;
    TextureManager* m_textures = nullptr;
    TextureHandle m_texture;
    int m_textureWidth = 0;
    bool m_dataDirty = false;
  };

} // namespace greeter
