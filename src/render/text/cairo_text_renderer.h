#pragma once

#include "render/core/color.h"
#include "render/core/mat3.h"
#include "render/core/renderer.h"
#include "render/core/texture_handle.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Forward declarations to avoid dragging Pango headers into every TU.
typedef struct _PangoContext PangoContext;
typedef struct _PangoFontMap PangoFontMap;
typedef struct _PangoLayout PangoLayout;

class RenderBackend;
class TextureManager;

// Pango/Cairo text rasterization and draw via RenderBackend.
// measure() has no GL context; draw() needs the backend current.
class CairoTextRenderer {
public:
  struct TextMetrics {
    float width = 0.0f;
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;       // negative, above baseline
    float bottom = 0.0f;    // positive, below baseline
    float inkTop = 0.0f;    // negative, visible ink above baseline
    float inkBottom = 0.0f; // positive, visible ink below baseline
    float inkLeft = 0.0f;   // visible ink left edge relative to layout origin
    float inkRight = 0.0f;  // visible ink right edge relative to layout origin
  };

  CairoTextRenderer();
  ~CairoTextRenderer();

  CairoTextRenderer(const CairoTextRenderer&) = delete;
  CairoTextRenderer& operator=(const CairoTextRenderer&) = delete;

  void initialize(RenderBackend* backend, TextureManager* textures);
  void cleanup();

  // HiDPI: raster at `scale × fontSize` pixels and shrink the quad by 1/scale.
  void setContentScale(float scale);
  void setFontFamily(std::string family);
  void notifyFontConfigChanged();

  [[nodiscard]] TextMetrics measure(
      std::string_view text, float fontSize, bool bold = false, float maxWidth = 0.0f, int maxLines = 0,
      TextAlign align = TextAlign::Start, std::string_view fontFamily = {}
  );
  [[nodiscard]] TextMetrics measureFont(float fontSize, bool bold = false) const;
  void measureCursorStops(
      std::string_view text, float fontSize, const std::vector<std::size_t>& byteOffsets, std::vector<float>& outStops,
      bool bold = false
  );

  void draw(
      float surfaceWidth, float surfaceHeight, float x, float baselineY, std::string_view text, float fontSize,
      const Color& color, const Mat3& transform, bool bold = false, float maxWidth = 0.0f, int maxLines = 0,
      TextAlign align = TextAlign::Start, std::string_view fontFamily = {}
  );

private:
  struct CacheKey {
    std::string text;
    std::string fontFamily;
    std::uint32_t sizeQ = 0;     // fontSize * 64 + 0.5
    std::uint32_t colorRgba = 0; // packed r<<24|g<<16|b<<8|a
    std::uint32_t maxWidthQ = 0; // maxWidth * 64 + 0.5, 0 = no limit
    std::uint16_t scaleQ = 0;    // contentScale * 64 + 0.5
    std::uint16_t maxLines = 0;  // 0 = no explicit limit (use '\n'-count fallback)
    TextAlign align = TextAlign::Start;
    bool bold = false;

    bool operator==(const CacheKey& other) const noexcept;
  };
  struct CacheKeyHash {
    std::size_t operator()(const CacheKey& k) const noexcept;
  };

  // Color-independent metrics cache (layout calls measure every frame).
  struct MetricsKey {
    std::string text;
    std::string fontFamily;
    std::uint32_t sizeQ = 0;
    std::uint32_t maxWidthQ = 0;
    std::uint16_t scaleQ = 0;
    std::uint16_t maxLines = 0;
    TextAlign align = TextAlign::Start;
    bool bold = false;

    bool operator==(const MetricsKey& other) const noexcept;
  };
  struct MetricsKeyHash {
    std::size_t operator()(const MetricsKey& k) const noexcept;
  };

  // LRU holds stable pointers into unordered_map keys.
  using LruList = std::list<const CacheKey*>;

  // Tall text is split into stacked tiles within the GL texture size limit.
  struct Tile {
    TextureHandle texture;
    int pixelHeight = 0;  // raster pixels
    int pixelYOffset = 0; // from top of full layout, in raster pixels
  };

  struct CacheEntry {
    std::vector<Tile> tiles;
    int pixelWidth = 0;   // total raster surface pixel width
    int pixelHeight = 0;  // total raster surface pixel height (sum of tiles)
    float baselinePx = 0; // baseline from top of full layout, in raster pixels
    float inkOffsetX = 0; // raster px from surface left to logical text origin
    TextMetrics metrics;  // logical metrics in logical (unscaled) pixels
    std::size_t bytes = 0;
    bool tinted = false; // true: alpha coverage, tint in shader; false: premul RGBA
    LruList::iterator lruIt;
  };

  using CacheMap = std::unordered_map<CacheKey, CacheEntry, CacheKeyHash>;
  using MetricsMap = std::unordered_map<MetricsKey, TextMetrics, MetricsKeyHash>;

  // Caller owns the returned layout (g_object_unref).
  PangoLayout* buildLayout(
      std::string_view text, float fontSize, bool bold, float maxWidthPxScaled, int maxLines, TextAlign align,
      std::string_view fontFamily = {}
  ) const;
  // tinted: A8 coverage for shader tint; else ARGB32 with color baked in.
  void rasterizeLayout(PangoLayout* layout, const Color& color, bool tinted, CacheEntry& entry);
  // Logical metrics from a laid-out PangoLayout.
  TextMetrics metricsFromLayout(PangoLayout* layout) const;

  CacheEntry* lookupOrRasterize(
      std::string_view text, float fontSize, bool bold, float maxWidth, int maxLines, TextAlign align,
      const Color& color, std::string_view fontFamily = {}
  );
  void touch(CacheMap::iterator it);
  void evict(CacheMap::iterator it);
  void evictIfNeeded();
  void clearCaches();

  float m_contentScale = 1.0f;
  bool m_fontConfigInitialized = false;
  std::string m_fontFamily = "sans-serif";

  PangoFontMap* m_fontMap = nullptr;      // owned
  PangoContext* m_pangoContext = nullptr; // owned
  RenderBackend* m_backend = nullptr;
  TextureManager* m_textureManager = nullptr;

  CacheMap m_cache;
  LruList m_lru;
  std::size_t m_cacheBytes = 0;
  int m_glMaxTextureSize = 0; // lazy-queried on first rasterize

  MetricsMap m_metricsCache;

  static constexpr std::size_t kMaxCacheEntries = 512;
  static constexpr std::size_t kMaxCacheBytes = 32 * 1024 * 1024;
  static constexpr std::size_t kMaxMetricsEntries = 1024;
};
