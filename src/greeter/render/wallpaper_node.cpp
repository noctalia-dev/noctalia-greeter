#include "greeter/render/wallpaper_node.h"

#include "greeter/render/wallpaper_program.h"
#include "render/backend/render_backend.h"

#include <GLES2/gl2.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace {
  class GreeterWallpaperExtension final : public RenderExtension {
  public:
    WallpaperProgram program;
    std::array<std::unique_ptr<RenderFramebuffer>, 2> buffers;
    void destroy() override {
      buffers = {};
      program.destroy();
    }
    void abandon() noexcept override {
      for (auto& buffer : buffers)
        if (buffer)
          buffer->abandon();
      buffers = {};
      program.abandon();
    }
    bool ensureBuffers(RenderBackend& backend, std::uint32_t width, std::uint32_t height) {
      for (auto& buffer : buffers) {
        if (buffer && buffer->width() == width && buffer->height() == height && buffer->valid())
          continue;
        buffer = backend.createFramebuffer(width, height);
        if (!buffer || !buffer->valid())
          return false;
      }
      return true;
    }
  };
} // namespace

void WallpaperNode::draw(RenderBackend& backend, const CustomDrawContext& context) const {
  if (m_sourceKind1 == WallpaperSourceKind::Image && !m_texture1)
    return;
  auto& extension = backend.extension<GreeterWallpaperExtension>();
  extension.program.ensureInitialized();
  const auto quad = [&](float sw, float sh, const Mat3& transform) {
    extension.program.draw(
        m_transition, m_sourceKind1, m_texture1, m_sourceColor1, m_sourceKind2, m_texture2, m_sourceColor2, sw, sh,
        context.width, context.height, m_imageWidth1, m_imageHeight1, m_imageWidth2, m_imageHeight2, m_progress,
        static_cast<float>(m_fillMode), m_params, m_fillColor, transform, m_span
    );
  };
  if (m_blurRadius <= 0.0F && m_tintColor.a <= 0.0F) {
    quad(context.surfaceWidth, context.surfaceHeight, context.transform);
    return;
  }
  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);
  GLint framebuffer;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
  const auto topLeft = context.transform.transformPoint(0.0F, 0.0F);
  const auto bottomRight = context.transform.transformPoint(context.width, context.height);
  const auto x = static_cast<GLint>(std::lround(topLeft.x * context.pixelScaleX));
  const auto y = static_cast<GLint>(std::lround(topLeft.y * context.pixelScaleY));
  const auto w = std::max(1, static_cast<GLint>(std::lround((bottomRight.x - topLeft.x) * context.pixelScaleX)));
  const auto h = std::max(1, static_cast<GLint>(std::lround((bottomRight.y - topLeft.y) * context.pixelScaleY)));
  const auto useWallpaperViewport = [&] { glViewport(x, viewport[3] - y - h, w, h); };
  bool blurred = false;
  const auto fw = static_cast<std::uint32_t>(std::max(1, w / 4));
  const auto fh = static_cast<std::uint32_t>(std::max(1, h / 4));
  if (m_blurRadius > 0.0F && extension.ensureBuffers(backend, fw, fh)) {
    const auto scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLint scissorBox[4];
    glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
    backend.disableScissor();
    backend.bindFramebuffer(*extension.buffers[0]);
    backend.setViewport(fw, fh);
    backend.clear(rgba(0, 0, 0, 0));
    quad(context.width, context.height, Mat3::identity());
    backend.bindFramebuffer(*extension.buffers[1]);
    backend.clear(rgba(0, 0, 0, 0));
    backend.drawFramebufferBlur(extension.buffers[0]->colorTexture(), fw, fh, 1, 0, m_blurRadius);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    backend.setViewport(static_cast<std::uint32_t>(viewport[2]), static_cast<std::uint32_t>(viewport[3]));
    if (scissor)
      backend.setScissor({scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]});
    useWallpaperViewport();
    backend.drawFramebufferBlur(extension.buffers[1]->colorTexture(), fw, fh, 0, 1, m_blurRadius);
    blurred = true;
  }
  if (!blurred)
    quad(context.surfaceWidth, context.surfaceHeight, context.transform);
  if (m_tintColor.a > 0.0F) {
    useWallpaperViewport();
    backend.setBlendMode(RenderBlendMode::StraightAlpha);
    backend.drawFullscreenTint(m_tintColor);
    backend.setBlendMode(RenderBlendMode::PremultipliedAlpha);
  }
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}
