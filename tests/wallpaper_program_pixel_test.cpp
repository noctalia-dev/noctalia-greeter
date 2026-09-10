#include "config/config_types.h"
#include "render/programs/wallpaper_program.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

  class EglUnavailable : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
  };

  struct Pixel {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 0;

    bool operator==(const Pixel&) const = default;
  };

  static_assert(sizeof(Pixel) == 4);

  constexpr Pixel kRed{255, 0, 0, 255};
  constexpr Pixel kBlue{0, 0, 255, 255};

  [[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

  void requireGl(const std::string_view operation) {
    if (const GLenum error = glGetError(); error != GL_NO_ERROR) {
      fail(std::string(operation) + " failed (GL error " + std::to_string(error) + ')');
    }
  }

  class EglContext {
  public:
    EglContext() {
      const auto getPlatformDisplay =
          reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
      if (getPlatformDisplay == nullptr) {
        throw EglUnavailable("EGL_EXT_platform_base is unavailable");
      }

      m_display = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
      EGLint major = 0;
      EGLint minor = 0;
      if (m_display == EGL_NO_DISPLAY || eglInitialize(m_display, &major, &minor) != EGL_TRUE) {
        throw EglUnavailable("surfaceless EGL initialization failed");
      }
      if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        throw EglUnavailable("OpenGL ES EGL API is unavailable");
      }

      constexpr EGLint configAttributes[] = {
          EGL_SURFACE_TYPE,
          EGL_PBUFFER_BIT,
          EGL_RENDERABLE_TYPE,
          EGL_OPENGL_ES2_BIT,
          EGL_RED_SIZE,
          8,
          EGL_GREEN_SIZE,
          8,
          EGL_BLUE_SIZE,
          8,
          EGL_ALPHA_SIZE,
          8,
          EGL_NONE,
      };
      EGLConfig config = nullptr;
      EGLint configCount = 0;
      if (eglChooseConfig(m_display, configAttributes, &config, 1, &configCount) != EGL_TRUE || configCount != 1) {
        throw EglUnavailable("an RGBA8 GLES2 pbuffer configuration is unavailable");
      }

      constexpr EGLint pbufferAttributes[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
      m_surface = eglCreatePbufferSurface(m_display, config, pbufferAttributes);
      constexpr EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
      m_context = eglCreateContext(m_display, config, EGL_NO_CONTEXT, contextAttributes);
      if (m_surface == EGL_NO_SURFACE
          || m_context == EGL_NO_CONTEXT
          || eglMakeCurrent(m_display, m_surface, m_surface, m_context) != EGL_TRUE) {
        throw EglUnavailable("a surfaceless GLES2 context is unavailable");
      }
    }

    ~EglContext() {
      if (m_display == EGL_NO_DISPLAY) {
        return;
      }
      eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
      if (m_context != EGL_NO_CONTEXT) {
        eglDestroyContext(m_display, m_context);
      }
      if (m_surface != EGL_NO_SURFACE) {
        eglDestroySurface(m_display, m_surface);
      }
      eglTerminate(m_display);
    }

    EglContext(const EglContext&) = delete;
    EglContext& operator=(const EglContext&) = delete;

  private:
    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLSurface m_surface = EGL_NO_SURFACE;
    EGLContext m_context = EGL_NO_CONTEXT;
  };

  [[nodiscard]] GLuint makeRedBlueTexture(const int width, const int height) {
    std::vector<Pixel> pixels(static_cast<std::size_t>(width * height));
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        pixels[static_cast<std::size_t>(y * width + x)] = x < width / 2 ? kRed : kBlue;
      }
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    requireGl("texture upload");
    return texture;
  }

  [[nodiscard]] GLuint makePatternTexture(const int width, const int height) {
    std::vector<Pixel> pixels(static_cast<std::size_t>(width * height));
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        pixels[static_cast<std::size_t>(y * width + x)] = Pixel{
            .red = static_cast<std::uint8_t>(x * 20 + 3),
            .green = static_cast<std::uint8_t>(y * 25 + 5),
            .blue = static_cast<std::uint8_t>(x * 13 + y * 7),
            .alpha = 255,
        };
      }
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    requireGl("pattern texture upload");
    return texture;
  }

  [[nodiscard]] std::vector<Pixel> render(
      WallpaperProgram& program, const GLuint texture, const int textureWidth, const int textureHeight, const int width,
      const int height, const WallpaperFillMode fillMode, const WallpaperSpanParams& span
  ) {
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const TextureId textureId(texture);
    program.draw(
        WallpaperTransition::Fade, WallpaperSourceKind::Image, textureId, {}, WallpaperSourceKind::Image, textureId, {},
        static_cast<float>(width), static_cast<float>(height), static_cast<float>(width), static_cast<float>(height),
        static_cast<float>(textureWidth), static_cast<float>(textureHeight), static_cast<float>(textureWidth),
        static_cast<float>(textureHeight), 0.0f, static_cast<float>(fillMode), {}, rgba(0.0f, 1.0f, 0.0f, 1.0f),
        Mat3::identity(), span
    );
    requireGl("wallpaper draw");

    std::vector<Pixel> pixels(static_cast<std::size_t>(width * height));
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    requireGl("pixel readback");
    return pixels;
  }

  [[nodiscard]] std::string pixelString(const Pixel& pixel) {
    return '('
        + std::to_string(pixel.red)
        + ','
        + std::to_string(pixel.green)
        + ','
        + std::to_string(pixel.blue)
        + ','
        + std::to_string(pixel.alpha)
        + ')';
  }

  void requireAll(const std::vector<Pixel>& pixels, const Pixel expected, const std::string_view name) {
    for (std::size_t i = 0; i < pixels.size(); ++i) {
      if (pixels[i] != expected) {
        fail(
            std::string(name)
            + ": pixel "
            + std::to_string(i)
            + " was "
            + pixelString(pixels[i])
            + ", expected "
            + pixelString(expected)
        );
      }
    }
  }

  void requireDesktopSlice(
      const std::vector<Pixel>& output, const std::vector<Pixel>& desktop, const int desktopWidth,
      const int desktopHeight, const int offsetX, const int offsetY, const int outputWidth, const int outputHeight,
      const std::string_view name
  ) {
    for (int y = 0; y < outputHeight; ++y) {
      // glReadPixels rows are bottom-up, while span offsets use the compositor's
      // top-down logical coordinate space.
      const int desktopReadbackY = desktopHeight - offsetY - outputHeight + y;
      for (int x = 0; x < outputWidth; ++x) {
        const std::size_t outputIndex = static_cast<std::size_t>(y * outputWidth + x);
        const std::size_t desktopIndex = static_cast<std::size_t>(desktopReadbackY * desktopWidth + offsetX + x);
        if (output[outputIndex] != desktop[desktopIndex]) {
          fail(
              std::string(name)
              + ": pixel "
              + std::to_string(outputIndex)
              + " was "
              + pixelString(output[outputIndex])
              + ", expected desktop pixel "
              + pixelString(desktop[desktopIndex])
          );
        }
      }
    }
  }

  void testTwoOutputSpan(WallpaperProgram& program) {
    constexpr int kTextureWidth = 8;
    constexpr int kTextureHeight = 4;
    constexpr int kOutputWidth = 4;
    constexpr int kOutputHeight = 4;
    const GLuint texture = makeRedBlueTexture(kTextureWidth, kTextureHeight);

    const auto left = render(
        program, texture, kTextureWidth, kTextureHeight, kOutputWidth, kOutputHeight, WallpaperFillMode::Span,
        {.offsetX = 0.0f,
         .offsetY = 0.0f,
         .monitorWidth = 4.0f,
         .monitorHeight = 4.0f,
         .totalWidth = 8.0f,
         .totalHeight = 4.0f}
    );
    const auto right = render(
        program, texture, kTextureWidth, kTextureHeight, kOutputWidth, kOutputHeight, WallpaperFillMode::Span,
        {.offsetX = 4.0f,
         .offsetY = 0.0f,
         .monitorWidth = 4.0f,
         .monitorHeight = 4.0f,
         .totalWidth = 8.0f,
         .totalHeight = 4.0f}
    );

    requireAll(left, kRed, "left span output");
    requireAll(right, kBlue, "right span output");
    glDeleteTextures(1, &texture);
  }

  void testStaggeredSpanMatchesDesktopCover(WallpaperProgram& program) {
    constexpr int kTextureWidth = 8;
    constexpr int kTextureHeight = 8;
    constexpr int kDesktopWidth = 8;
    constexpr int kDesktopHeight = 6;
    const GLuint texture = makePatternTexture(kTextureWidth, kTextureHeight);

    const auto desktop = render(
        program, texture, kTextureWidth, kTextureHeight, kDesktopWidth, kDesktopHeight, WallpaperFillMode::Crop, {}
    );
    const auto lowerLeft = render(
        program, texture, kTextureWidth, kTextureHeight, 3, 4, WallpaperFillMode::Span,
        {.offsetX = 0.0f,
         .offsetY = 2.0f,
         .monitorWidth = 3.0f,
         .monitorHeight = 4.0f,
         .totalWidth = 8.0f,
         .totalHeight = 6.0f}
    );
    const auto right = render(
        program, texture, kTextureWidth, kTextureHeight, 5, 6, WallpaperFillMode::Span,
        {.offsetX = 3.0f,
         .offsetY = 0.0f,
         .monitorWidth = 5.0f,
         .monitorHeight = 6.0f,
         .totalWidth = 8.0f,
         .totalHeight = 6.0f}
    );

    requireDesktopSlice(lowerLeft, desktop, kDesktopWidth, kDesktopHeight, 0, 2, 3, 4, "staggered left output");
    requireDesktopSlice(right, desktop, kDesktopWidth, kDesktopHeight, 3, 0, 5, 6, "staggered right output");
    glDeleteTextures(1, &texture);
  }

  void testZeroGeometryFallsBackToCrop(WallpaperProgram& program) {
    constexpr int kTextureWidth = 4;
    constexpr int kTextureHeight = 2;
    constexpr int kOutputSize = 8;
    const GLuint texture = makeRedBlueTexture(kTextureWidth, kTextureHeight);
    const auto crop =
        render(program, texture, kTextureWidth, kTextureHeight, kOutputSize, kOutputSize, WallpaperFillMode::Crop, {});
    const auto spanWithoutGeometry =
        render(program, texture, kTextureWidth, kTextureHeight, kOutputSize, kOutputSize, WallpaperFillMode::Span, {});
    if (spanWithoutGeometry != crop) {
      fail("span without geometry did not match crop");
    }
    glDeleteTextures(1, &texture);
  }

  void testRepeatStillTiles(WallpaperProgram& program) {
    constexpr int kTextureWidth = 2;
    constexpr int kTextureHeight = 1;
    constexpr int kOutputWidth = 8;
    constexpr int kOutputHeight = 2;
    const GLuint texture = makeRedBlueTexture(kTextureWidth, kTextureHeight);
    const auto repeat = render(
        program, texture, kTextureWidth, kTextureHeight, kOutputWidth, kOutputHeight, WallpaperFillMode::Repeat, {}
    );

    for (int y = 0; y < kOutputHeight; ++y) {
      for (int x = 0; x < kOutputWidth; ++x) {
        const Pixel expected = x % 2 == 0 ? kRed : kBlue;
        const std::size_t index = static_cast<std::size_t>(y * kOutputWidth + x);
        if (repeat[index] != expected) {
          fail("repeat mode did not preserve tiling at pixel " + std::to_string(index));
        }
      }
    }
    glDeleteTextures(1, &texture);
  }

} // namespace

int main() {
  try {
    EglContext egl;
    WallpaperProgram program;
    program.ensureInitialized();
    requireGl("shader initialization");
    testTwoOutputSpan(program);
    testStaggeredSpanMatchesDesktopCover(program);
    testZeroGeometryFallsBackToCrop(program);
    testRepeatStillTiles(program);
    program.destroy();
    return EXIT_SUCCESS;
  } catch (const EglUnavailable& error) {
    std::cout << "SKIP: " << error.what() << '\n';
    return 77;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
