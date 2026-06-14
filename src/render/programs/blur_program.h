#pragma once

#include "render/core/shader_program.h"
#include "render/core/texture_handle.h"

#include <GLES2/gl2.h>
#include <cstdint>

class BlurProgram {
public:
  void ensureInitialized();
  void destroy();

  // Separable Gaussian blur; dirX/dirY pick axis, radius is kernel half-width.
  void draw(TextureId srcTex, std::uint32_t width, std::uint32_t height, float dirX, float dirY, float radius) const;

private:
  ShaderProgram m_program;
  GLint m_posLoc = -1;
  GLint m_texLoc = -1;
  GLint m_texelSzLoc = -1;
  GLint m_directionLoc = -1;
  GLint m_radiusLoc = -1;
};
