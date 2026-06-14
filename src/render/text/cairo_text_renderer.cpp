#include "render/text/cairo_text_renderer.h"

#include "core/log.h"
#include "render/backend/render_backend.h"
#include "render/core/texture_manager.h"

#include <algorithm>
#include <cairo.h>
#include <cmath>
#include <cstring>
#include <fontconfig/fontconfig.h>
#include <functional>
#include <limits>
#include <pango/pango-attributes.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#include <vector>

namespace {

  constexpr Logger kLog("text");

  constexpr std::uint32_t kSizeQuant = 64;
  constexpr std::uint32_t kScaleQuant = 64;
  constexpr float kAxisAlignedEpsilon = 0.0001f;

  inline std::uint32_t quantizeSize(float v) {
    return static_cast<std::uint32_t>(std::max(0.0f, v) * static_cast<float>(kSizeQuant) + 0.5f);
  }

  inline std::uint16_t quantizeScale(float v) {
    return static_cast<std::uint16_t>(std::max(0.0f, v) * static_cast<float>(kScaleQuant) + 0.5f);
  }

  float snapToBufferPixel(float value, float scale) {
    const float safeScale = std::max(1.0f, scale);
    return std::round(value * safeScale) / safeScale;
  }

  bool isAxisAligned(const Mat3& transform) {
    return std::abs(transform.m[1]) <= kAxisAlignedEpsilon && std::abs(transform.m[3]) <= kAxisAlignedEpsilon;
  }

  void hashCombine(std::size_t& seed, std::size_t v) { seed ^= v + 0x9E3779B97F4A7C15ULL + (seed << 12) + (seed >> 4); }

  // Pack RGB in key; alpha applied at draw time via u_opacity.
  std::uint32_t packColorRgb(const Color& c) {
    const auto clamp8 = [](float v) -> std::uint32_t {
      const float s = std::clamp(v, 0.0f, 1.0f);
      return static_cast<std::uint32_t>(s * 255.0f + 0.5f);
    };
    return (clamp8(c.r) << 24) | (clamp8(c.g) << 16) | (clamp8(c.b) << 8) | 0xFFu;
  }

  // Swap BGRA<->RGBA in place on a premultiplied ARGB32 Cairo surface buffer.
  void swizzleBgraToRgba(unsigned char* data, int width, int height, int stride) {
    for (int y = 0; y < height; ++y) {
      unsigned char* row = data + y * stride;
      for (int x = 0; x < width; ++x) {
        unsigned char* p = row + x * 4;
        std::swap(p[0], p[2]); // B <-> R; G and A unchanged
      }
    }
  }

  // Heuristic: emoji/symbol ranges rasterize as RGBA; else A8 + shader tint.
  bool containsColorGlyph(std::string_view text) {
    const auto* s = reinterpret_cast<const unsigned char*>(text.data());
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
      unsigned char b = s[i];
      char32_t cp = 0;
      int len = 1;
      if (b < 0x80) {
        cp = b;
      } else if ((b & 0xE0) == 0xC0 && i + 1 < n) {
        cp = static_cast<char32_t>((b & 0x1F) << 6 | (s[i + 1] & 0x3F));
        len = 2;
      } else if ((b & 0xF0) == 0xE0 && i + 2 < n) {
        cp = static_cast<char32_t>((b & 0x0F) << 12 | (s[i + 1] & 0x3F) << 6 | (s[i + 2] & 0x3F));
        len = 3;
      } else if ((b & 0xF8) == 0xF0 && i + 3 < n) {
        cp = static_cast<char32_t>(
            (b & 0x07) << 18 | (s[i + 1] & 0x3F) << 12 | (s[i + 2] & 0x3F) << 6 | (s[i + 3] & 0x3F)
        );
        len = 4;
      } else {
        return true; // malformed, be safe
      }
      i += static_cast<std::size_t>(len);
      if (cp >= 0x2600 && cp <= 0x27BF)
        return true; // misc symbols + dingbats
      if (cp >= 0x1F000 && cp <= 0x1FFFF)
        return true; // emoji planes
      if (cp >= 0x1F900 && cp <= 0x1F9FF)
        return true; // supplemental symbols
    }
    return false;
  }

} // namespace

// CacheKey equality/hash

bool CairoTextRenderer::CacheKey::operator==(const CacheKey& other) const noexcept {
  return bold == other.bold
      && sizeQ == other.sizeQ
      && scaleQ == other.scaleQ
      && maxWidthQ == other.maxWidthQ
      && maxLines == other.maxLines
      && align == other.align
      && colorRgba == other.colorRgba
      && text == other.text
      && fontFamily == other.fontFamily;
}

std::size_t CairoTextRenderer::CacheKeyHash::operator()(const CacheKey& k) const noexcept {
  std::size_t seed = std::hash<std::string>{}(k.text);
  hashCombine(seed, std::hash<std::string>{}(k.fontFamily));
  hashCombine(seed, std::hash<std::uint32_t>{}(k.sizeQ));
  hashCombine(seed, std::hash<std::uint32_t>{}(k.maxWidthQ));
  hashCombine(seed, std::hash<std::uint16_t>{}(k.scaleQ));
  hashCombine(seed, std::hash<std::uint16_t>{}(k.maxLines));
  hashCombine(seed, std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(k.align)));
  hashCombine(seed, std::hash<std::uint32_t>{}(k.colorRgba));
  hashCombine(seed, std::hash<bool>{}(k.bold));
  return seed;
}

bool CairoTextRenderer::MetricsKey::operator==(const MetricsKey& other) const noexcept {
  return bold == other.bold
      && sizeQ == other.sizeQ
      && scaleQ == other.scaleQ
      && maxWidthQ == other.maxWidthQ
      && maxLines == other.maxLines
      && align == other.align
      && text == other.text
      && fontFamily == other.fontFamily;
}

std::size_t CairoTextRenderer::MetricsKeyHash::operator()(const MetricsKey& k) const noexcept {
  std::size_t seed = std::hash<std::string>{}(k.text);
  hashCombine(seed, std::hash<std::string>{}(k.fontFamily));
  hashCombine(seed, std::hash<std::uint32_t>{}(k.sizeQ));
  hashCombine(seed, std::hash<std::uint32_t>{}(k.maxWidthQ));
  hashCombine(seed, std::hash<std::uint16_t>{}(k.scaleQ));
  hashCombine(seed, std::hash<std::uint16_t>{}(k.maxLines));
  hashCombine(seed, std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(k.align)));
  hashCombine(seed, std::hash<bool>{}(k.bold));
  return seed;
}

// Lifecycle

CairoTextRenderer::CairoTextRenderer() = default;

CairoTextRenderer::~CairoTextRenderer() { cleanup(); }

void CairoTextRenderer::initialize(RenderBackend* backend, TextureManager* textures) {
  m_backend = backend;
  m_textureManager = textures;

  if (FcInit()) {
    m_fontConfigInitialized = true;
  } else {
    kLog.warn("fontconfig initialization failed");
  }

  if (cairo_version() < CAIRO_VERSION_ENCODE(1, 18, 0)) {
    kLog.warn("cairo version {} (<1.18), COLR v1 color emoji will not render", cairo_version_string());
  }

  m_fontMap = pango_cairo_font_map_new();
  m_pangoContext = pango_font_map_create_context(m_fontMap);

  // Grayscale AA + full hinting so measure() and draw() agree on widths.
  cairo_font_options_t* fontOptions = cairo_font_options_create();
  cairo_font_options_set_antialias(fontOptions, CAIRO_ANTIALIAS_GRAY);
  cairo_font_options_set_hint_style(fontOptions, CAIRO_HINT_STYLE_FULL);
  cairo_font_options_set_hint_metrics(fontOptions, CAIRO_HINT_METRICS_ON);
  pango_cairo_context_set_font_options(m_pangoContext, fontOptions);
  cairo_font_options_destroy(fontOptions);

  // Reserve cache buckets so LRU iterators stay valid.
  m_cache.max_load_factor(1.0f);
  m_cache.reserve(kMaxCacheEntries + 16);

  m_metricsCache.max_load_factor(1.0f);
  m_metricsCache.reserve(kMaxMetricsEntries + 16);
}

void CairoTextRenderer::cleanup() {
  clearCaches();

  if (m_pangoContext != nullptr) {
    g_object_unref(m_pangoContext);
    m_pangoContext = nullptr;
  }
  if (m_fontMap != nullptr) {
    g_object_unref(m_fontMap);
    m_fontMap = nullptr;
  }
  if (m_fontConfigInitialized) {
    FcFini();
    m_fontConfigInitialized = false;
  }
  m_backend = nullptr;
  m_textureManager = nullptr;
}

void CairoTextRenderer::clearCaches() {
  for (auto& [key, entry] : m_cache) {
    for (auto& tile : entry.tiles) {
      if (m_textureManager != nullptr) {
        m_textureManager->unload(tile.texture);
      }
    }
  }
  m_cache.clear();
  m_lru.clear();
  m_cacheBytes = 0;
  m_metricsCache.clear();
}

void CairoTextRenderer::setContentScale(float scale) {
  if (scale <= 0.0f) {
    return;
  }
  m_contentScale = scale;
}

void CairoTextRenderer::setFontFamily(std::string family) {
  if (family.empty()) {
    family = "sans-serif";
  }
  if (m_fontFamily == family) {
    return;
  }
  m_fontFamily = std::move(family);
  clearCaches();
}

void CairoTextRenderer::notifyFontConfigChanged() {
  if (m_fontMap != nullptr && PANGO_IS_FC_FONT_MAP(m_fontMap)) {
    // PangoFcFontMap caches its fontconfig view; this forces it to re-read the
    // current FcConfig (which is where FcConfigAppFontAddFile added the font).
    pango_fc_font_map_config_changed(PANGO_FC_FONT_MAP(m_fontMap));
  }
  clearCaches();
}

// Layout construction

PangoLayout* CairoTextRenderer::buildLayout(
    std::string_view text, float fontSize, bool bold, float maxWidthPxScaled, int maxLines, TextAlign align,
    std::string_view fontFamily
) const {
  PangoLayout* layout = pango_layout_new(m_pangoContext);

  const float rasterSize = std::max(1.0f, fontSize * m_contentScale);
  PangoFontDescription* desc = pango_font_description_new();
  std::string fontFamilyStr;
  if (!fontFamily.empty()) {
    fontFamilyStr.assign(fontFamily);
  }
  pango_font_description_set_family(desc, fontFamilyStr.empty() ? m_fontFamily.c_str() : fontFamilyStr.c_str());
  pango_font_description_set_weight(desc, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
  pango_font_description_set_absolute_size(desc, static_cast<double>(rasterSize) * PANGO_SCALE);
  pango_layout_set_font_description(layout, desc);
  pango_font_description_free(desc);

  pango_layout_set_text(layout, text.data(), static_cast<int>(text.size()));

  // Multi-line '\n' breaks; set_height caps lines (kHardMaxLines safety limit).
  if (maxWidthPxScaled > 0.0f) {
    // Avoid Pango inserting hyphens at intra-word line breaks (looks like stray
    // "-" in wrapped UI text).
    PangoAttrList* attrs = pango_attr_list_new();
    PangoAttribute* hyphens = pango_attr_insert_hyphens_new(FALSE);
    hyphens->start_index = 0;
    hyphens->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
    pango_attr_list_insert(attrs, hyphens);
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);

    constexpr int kHardMaxLines = 500;
    int lineBudget = maxLines > 0 ? maxLines : 1 + static_cast<int>(std::count(text.begin(), text.end(), '\n'));
    lineBudget = std::min(lineBudget, kHardMaxLines);
    pango_layout_set_width(layout, static_cast<int>(maxWidthPxScaled * PANGO_SCALE));
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_height(layout, -lineBudget);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
  } else {
    pango_layout_set_width(layout, -1);
  }

  PangoAlignment pangoAlign = PANGO_ALIGN_LEFT;
  if (align == TextAlign::Center) {
    pangoAlign = PANGO_ALIGN_CENTER;
  } else if (align == TextAlign::End) {
    pangoAlign = PANGO_ALIGN_RIGHT;
  }
  pango_layout_set_alignment(layout, pangoAlign);

  return layout;
}

CairoTextRenderer::TextMetrics CairoTextRenderer::metricsFromLayout(PangoLayout* layout) const {
  PangoRectangle ink;
  PangoRectangle logical;
  pango_layout_get_extents(layout, &ink, &logical);
  const int baselinePango = pango_layout_get_baseline(layout);

  const float invScale = 1.0f / m_contentScale;
  const float pscale = 1.0f / static_cast<float>(PANGO_SCALE);

  const float width = static_cast<float>(logical.width) * pscale * invScale;
  // Pango logical rect y is 0 at top of layout box; baseline is offset from
  // top.
  const float ascent = static_cast<float>(baselinePango - logical.y) * pscale * invScale;
  const float descent = static_cast<float>(logical.height - (baselinePango - logical.y)) * pscale * invScale;
  const float inkTop = static_cast<float>(ink.y - baselinePango) * pscale * invScale;
  const float inkBottom = static_cast<float>(ink.y + ink.height - baselinePango) * pscale * invScale;
  const float inkLeft = static_cast<float>(ink.x) * pscale * invScale;
  const float inkRight = static_cast<float>(ink.x + ink.width) * pscale * invScale;

  TextMetrics m;
  m.width = width;
  m.left = 0.0f;
  m.right = width;
  m.top = -ascent;    // above baseline -> negative
  m.bottom = descent; // below baseline -> positive
  m.inkTop = inkTop;
  m.inkBottom = inkBottom;
  m.inkLeft = inkLeft;
  m.inkRight = inkRight;
  return m;
}

// measure / truncate

CairoTextRenderer::TextMetrics CairoTextRenderer::measure(
    std::string_view text, float fontSize, bool bold, float maxWidth, int maxLines, TextAlign align,
    std::string_view fontFamily
) {
  if (m_pangoContext == nullptr || text.empty()) {
    return {};
  }

  MetricsKey key;
  key.text.assign(text);
  key.fontFamily.assign(fontFamily);
  key.sizeQ = quantizeSize(fontSize);
  key.maxWidthQ = quantizeSize(std::max(0.0f, maxWidth));
  key.scaleQ = quantizeScale(m_contentScale);
  key.maxLines = static_cast<std::uint16_t>(std::max(0, maxLines));
  key.align = align;
  key.bold = bold;

  auto it = m_metricsCache.find(key);
  if (it != m_metricsCache.end()) {
    return it->second;
  }

  PangoLayout* layout = buildLayout(text, fontSize, bold, maxWidth * m_contentScale, maxLines, align, fontFamily);
  const auto metrics = metricsFromLayout(layout);
  g_object_unref(layout);

  if (m_metricsCache.size() >= kMaxMetricsEntries) {
    m_metricsCache.clear();
  }
  m_metricsCache.emplace(std::move(key), metrics);
  return metrics;
}

CairoTextRenderer::TextMetrics CairoTextRenderer::measureFont(float fontSize, bool bold) const {
  if (m_pangoContext == nullptr) {
    return {};
  }

  const float rasterSize = std::max(1.0f, fontSize * m_contentScale);
  PangoFontDescription* desc = pango_font_description_new();
  pango_font_description_set_family(desc, m_fontFamily.c_str());
  pango_font_description_set_weight(desc, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
  pango_font_description_set_absolute_size(desc, static_cast<double>(rasterSize) * PANGO_SCALE);

  PangoFontMetrics* metrics = pango_context_get_metrics(m_pangoContext, desc, pango_language_get_default());
  pango_font_description_free(desc);
  if (metrics == nullptr) {
    return {};
  }

  const float invScale = 1.0f / m_contentScale;
  const float pscale = 1.0f / static_cast<float>(PANGO_SCALE);
  const float ascent = static_cast<float>(pango_font_metrics_get_ascent(metrics)) * pscale * invScale;
  const float descent = static_cast<float>(pango_font_metrics_get_descent(metrics)) * pscale * invScale;
  pango_font_metrics_unref(metrics);

  TextMetrics out;
  out.top = -ascent;
  out.bottom = descent;
  return out;
}

void CairoTextRenderer::measureCursorStops(
    std::string_view text, float fontSize, const std::vector<std::size_t>& byteOffsets, std::vector<float>& outStops,
    bool bold
) {
  outStops.clear();
  outStops.reserve(byteOffsets.size());

  if (byteOffsets.empty()) {
    return;
  }
  if (m_pangoContext == nullptr || text.empty()) {
    outStops.resize(byteOffsets.size(), 0.0f);
    return;
  }

  PangoLayout* layout = buildLayout(text, fontSize, bold, 0.0f, 0, TextAlign::Start);
  const float invScale = 1.0f / m_contentScale;
  const float pscale = 1.0f / static_cast<float>(PANGO_SCALE);
  for (const std::size_t offset : byteOffsets) {
    const std::size_t clampedOffset = std::min(offset, text.size());
    const int index = clampedOffset > static_cast<std::size_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(clampedOffset);
    PangoRectangle strong{};
    PangoRectangle weak{};
    pango_layout_get_cursor_pos(layout, index, &strong, &weak);
    outStops.push_back(static_cast<float>(strong.x) * pscale * invScale);
  }
  g_object_unref(layout);
}

// Rasterization

void CairoTextRenderer::rasterizeLayout(PangoLayout* layout, const Color& color, bool tinted, CacheEntry& entry) {
  entry.tinted = tinted;
  // Ceil logical rect; per-line x offsets center narrower lines in the surface.
  PangoRectangle inkLayout;
  PangoRectangle logicalLayout;
  pango_layout_get_extents(layout, &inkLayout, &logicalLayout);
  int pxWidth = (logicalLayout.width + PANGO_SCALE - 1) / PANGO_SCALE;
  int pxHeight = (logicalLayout.height + PANGO_SCALE - 1) / PANGO_SCALE;
  const int blockLeftPx = logicalLayout.x / PANGO_SCALE;

  const int extraLeftPx = (std::max(0, logicalLayout.x - inkLayout.x) + PANGO_SCALE - 1) / PANGO_SCALE;
  const int extraRightPx =
      (std::max(0, (inkLayout.x + inkLayout.width) - (logicalLayout.x + logicalLayout.width)) + PANGO_SCALE - 1)
      / PANGO_SCALE;
  const int extraTopPx = (std::max(0, logicalLayout.y - inkLayout.y) + PANGO_SCALE - 1) / PANGO_SCALE;
  const int extraBottomPx =
      (std::max(0, (inkLayout.y + inkLayout.height) - (logicalLayout.y + logicalLayout.height)) + PANGO_SCALE - 1)
      / PANGO_SCALE;
  pxWidth += extraLeftPx + extraRightPx;
  pxHeight += extraTopPx + extraBottomPx;
  entry.inkOffsetX = static_cast<float>(extraLeftPx);

  // Guard against zero-sized surfaces Cairo rejects.
  pxWidth = std::max(1, pxWidth);
  pxHeight = std::max(1, pxHeight);

  const int baselinePango = pango_layout_get_baseline(layout);
  entry.baselinePx =
      static_cast<float>(baselinePango) / static_cast<float>(PANGO_SCALE) + static_cast<float>(extraTopPx);

  if (m_glMaxTextureSize <= 0 && m_backend != nullptr) {
    m_glMaxTextureSize = m_backend->maxTextureSize();
  }
  if (m_glMaxTextureSize <= 0) {
    m_glMaxTextureSize = 2048; // conservative fallback
  }
  const int maxTex = m_glMaxTextureSize;

  // Clip over-wide lines; horizontal tiling would split glyphs across textures.
  if (pxWidth > maxTex) {
    kLog.warn("text width {}px exceeds backend texture limit {}, clipping", pxWidth, maxTex);
    pxWidth = maxTex;
  }

  entry.pixelWidth = pxWidth;
  entry.pixelHeight = pxHeight;
  entry.tiles.clear();
  entry.bytes = 0;

  // Tall layouts tile by line (show_layout_line per tile, max height maxTex).
  struct LineSlot {
    PangoLayoutLine* line = nullptr;
    int xLeftPx = 0;    // alignment offset of this line within the layout (pixels)
    int yTopPx = 0;     // top of line in full-layout raster pixels
    int baselinePx = 0; // baseline of line in full-layout raster pixels
  };
  struct TilePlan {
    int yTopPx = 0;
    int heightPx = 0;
    std::vector<LineSlot> lines;
  };

  std::vector<TilePlan> plan;
  {
    TilePlan current;
    current.yTopPx = 0;

    PangoLayoutIter* iter = pango_layout_get_iter(layout);
    do {
      PangoRectangle logical;
      pango_layout_iter_get_line_extents(iter, nullptr, &logical);
      const int lineTopPx = logical.y / PANGO_SCALE;
      const int lineBottomPx = (logical.y + logical.height + PANGO_SCALE - 1) / PANGO_SCALE;
      const int lineBaselinePx = pango_layout_iter_get_baseline(iter) / PANGO_SCALE;

      // If adding this line would push the current tile past maxTex, close
      // the current tile and start a new one at this line's top.
      if (!current.lines.empty() && (lineBottomPx - current.yTopPx) > maxTex) {
        plan.push_back(std::move(current));
        current = TilePlan{};
        current.yTopPx = lineTopPx;
      }

      LineSlot slot;
      slot.line = pango_layout_iter_get_line_readonly(iter);
      slot.xLeftPx = logical.x / PANGO_SCALE;
      slot.yTopPx = lineTopPx;
      slot.baselinePx = lineBaselinePx;
      current.lines.push_back(slot);
    } while (pango_layout_iter_next_line(iter));
    pango_layout_iter_free(iter);

    if (!current.lines.empty()) {
      plan.push_back(std::move(current));
    }
  }

  // Tile heights abut on pixel boundaries through the last descender row.
  for (std::size_t i = 0; i < plan.size(); ++i) {
    const int nextTop = (i + 1 < plan.size()) ? plan[i + 1].yTopPx : pxHeight;
    plan[i].heightPx = std::max(1, nextTop - plan[i].yTopPx);
  }

  if (plan.empty()) {
    // Nothing to render (empty layout). Leave entry.tiles empty.
    entry.metrics = metricsFromLayout(layout);
    return;
  }

  // Tinted: A8 coverage; untinted: ARGB32 with color (COLR emoji).
  const int bytesPerPixel = tinted ? 1 : 4;
  const int tightRowBytes = pxWidth * bytesPerPixel;
  std::vector<unsigned char> tight;

  for (const auto& tilePlan : plan) {
    const int tileH = tilePlan.heightPx;

    const cairo_format_t fmt = tinted ? CAIRO_FORMAT_A8 : CAIRO_FORMAT_ARGB32;
    cairo_surface_t* surface = cairo_image_surface_create(fmt, pxWidth, tileH);
    cairo_t* cr = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    if (tinted) {
      // A8: rgb ignored, opaque alpha so show_glyphs writes coverage = 1.
      cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    } else {
      cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    }

    // show_layout_line places the line baseline at the current device origin.
    for (const auto& ls : tilePlan.lines) {
      const double baselineInTile = static_cast<double>(ls.baselinePx - tilePlan.yTopPx);
      cairo_save(cr);
      cairo_translate(cr, static_cast<double>(ls.xLeftPx - blockLeftPx), baselineInTile);
      pango_cairo_show_layout_line(cr, ls.line);
      cairo_restore(cr);
    }

    cairo_destroy(cr);
    cairo_surface_flush(surface);

    const int stride = cairo_image_surface_get_stride(surface);
    unsigned char* data = cairo_image_surface_get_data(surface);
    if (!tinted) {
      swizzleBgraToRgba(data, pxWidth, tileH, stride);
    }

    // Repack tightly because the backend upload path expects contiguous rows.
    tight.resize(static_cast<std::size_t>(tightRowBytes) * static_cast<std::size_t>(tileH));
    for (int y = 0; y < tileH; ++y) {
      std::memcpy(tight.data() + y * tightRowBytes, data + y * stride, static_cast<std::size_t>(tightRowBytes));
    }
    cairo_surface_destroy(surface);

    if (m_textureManager == nullptr) {
      entry.tiles.clear();
      entry.bytes = 0;
      entry.metrics = metricsFromLayout(layout);
      return;
    }

    TextureHandle texture = m_textureManager->loadFromPixels(
        tight.data(), pxWidth, tileH, tinted ? TextureDataFormat::Alpha : TextureDataFormat::Rgba, TextureFilter::Linear
    );
    if (texture.id == 0) {
      for (auto& tile : entry.tiles) {
        m_textureManager->unload(tile.texture);
      }
      entry.tiles.clear();
      entry.bytes = 0;
      entry.metrics = metricsFromLayout(layout);
      return;
    }

    Tile tile;
    tile.texture = texture;
    tile.pixelHeight = tileH;
    tile.pixelYOffset = tilePlan.yTopPx;
    entry.tiles.push_back(tile);
    entry.bytes += static_cast<std::size_t>(tightRowBytes) * static_cast<std::size_t>(tileH);
  }

  entry.metrics = metricsFromLayout(layout);
}

// Cache management

void CairoTextRenderer::touch(CacheMap::iterator it) {
  // Splice the LRU node to the front (most-recently-used).
  m_lru.splice(m_lru.begin(), m_lru, it->second.lruIt);
}

void CairoTextRenderer::evict(CacheMap::iterator it) {
  for (auto& tile : it->second.tiles) {
    if (m_textureManager != nullptr) {
      m_textureManager->unload(tile.texture);
    }
  }
  m_cacheBytes -= it->second.bytes;
  m_lru.erase(it->second.lruIt);
  m_cache.erase(it);
}

void CairoTextRenderer::evictIfNeeded() {
  // Keep LRU front so a huge new entry cannot evict itself.
  while (m_lru.size() > 1 && (m_cache.size() > kMaxCacheEntries || m_cacheBytes > kMaxCacheBytes)) {
    const CacheKey* keyPtr = m_lru.back();
    auto mapIt = m_cache.find(*keyPtr);
    if (mapIt == m_cache.end()) {
      m_lru.pop_back();
      continue;
    }
    evict(mapIt);
  }
}

CairoTextRenderer::CacheEntry* CairoTextRenderer::lookupOrRasterize(
    std::string_view text, float fontSize, bool bold, float maxWidth, int maxLines, TextAlign align, const Color& color,
    std::string_view fontFamily
) {
  // A8 keys are color-independent; RGBA keys include rgb for COLR emoji paths.
  const bool tinted = !containsColorGlyph(text);

  CacheKey key;
  key.text.assign(text);
  key.fontFamily.assign(fontFamily);
  key.sizeQ = quantizeSize(fontSize);
  key.maxWidthQ = quantizeSize(std::max(0.0f, maxWidth));
  key.scaleQ = quantizeScale(m_contentScale);
  key.maxLines = static_cast<std::uint16_t>(std::max(0, maxLines));
  key.align = align;
  key.bold = bold;
  key.colorRgba = tinted ? 0u : packColorRgb(color);

  auto it = m_cache.find(key);
  if (it != m_cache.end()) {
    touch(it);
    return &it->second;
  }

  PangoLayout* layout = buildLayout(text, fontSize, bold, maxWidth * m_contentScale, maxLines, align, fontFamily);
  Color rasterColor = color;
  if (!tinted) {
    rasterColor.a = 1.0f;
  }
  CacheEntry entry{};
  rasterizeLayout(layout, rasterColor, tinted, entry);
  g_object_unref(layout);

  MetricsKey mkey;
  mkey.text = key.text;
  mkey.fontFamily = key.fontFamily;
  mkey.sizeQ = key.sizeQ;
  mkey.maxWidthQ = key.maxWidthQ;
  mkey.scaleQ = key.scaleQ;
  mkey.maxLines = key.maxLines;
  mkey.align = key.align;
  mkey.bold = key.bold;
  if (m_metricsCache.size() >= kMaxMetricsEntries) {
    m_metricsCache.clear();
  }
  m_metricsCache.emplace(std::move(mkey), entry.metrics);

  auto [ins, inserted] = m_cache.emplace(std::move(key), std::move(entry));
  m_lru.push_front(&ins->first);
  ins->second.lruIt = m_lru.begin();
  m_cacheBytes += ins->second.bytes;

  evictIfNeeded();
  return &ins->second;
}

// draw

void CairoTextRenderer::draw(
    float surfaceWidth, float surfaceHeight, float x, float baselineY, std::string_view text, float fontSize,
    const Color& color, const Mat3& transform, bool bold, float maxWidth, int maxLines, TextAlign align,
    std::string_view fontFamily
) {
  if (m_pangoContext == nullptr || m_backend == nullptr || text.empty()) {
    return;
  }

  CacheEntry* entry = lookupOrRasterize(text, fontSize, bold, maxWidth, maxLines, align, color, fontFamily);
  if (entry == nullptr || entry->tiles.empty()) {
    return;
  }
  if (entry->tiles.size() > 1) {
    kLog.warn(
        "draw tiles={} pxW={} pxH={} baseXY=({}, {})", entry->tiles.size(), entry->pixelWidth, entry->pixelHeight, x,
        baselineY
    );
  }

  const float invScale = 1.0f / m_contentScale;
  const float quadW = static_cast<float>(entry->pixelWidth) * invScale;
  const float baselineLocal = entry->baselinePx * invScale;

  // Align the raster baseline row to baselineY in local space.
  const float inkOffX = entry->inkOffsetX * invScale;
  const Mat3 localTranslation = Mat3::translation(x - inkOffX, baselineY - baselineLocal);
  Mat3 baseWorld = transform * localTranslation;

  if (isAxisAligned(baseWorld)) {
    baseWorld.m[6] = snapToBufferPixel(baseWorld.m[6], m_contentScale);
    baseWorld.m[7] = snapToBufferPixel(baseWorld.m[7], m_contentScale);
  }

  // One quad per tile; integer pixelYOffset avoids seams at fractional scale.
  for (const auto& tile : entry->tiles) {
    const float tileYLocal = static_cast<float>(tile.pixelYOffset) * invScale;
    const float tileH = static_cast<float>(tile.pixelHeight) * invScale;
    const Mat3 tileWorld = baseWorld * Mat3::translation(0.0f, tileYLocal);
    if (entry->tinted) {
      m_backend->drawGlyph(
          RenderGlyphDraw{
              .texture = tile.texture.id,
              .surfaceWidth = surfaceWidth,
              .surfaceHeight = surfaceHeight,
              .width = quadW,
              .height = tileH,
              .opacity = 1.0f,
              .tint = color,
              .tinted = true,
              .transform = tileWorld,
          }
      );
    } else {
      // RGBA entries are rasterized at alpha=1.0 and color-keyed by rgb, so
      // the caller's alpha is applied here as opacity.
      m_backend->drawGlyph(
          RenderGlyphDraw{
              .texture = tile.texture.id,
              .surfaceWidth = surfaceWidth,
              .surfaceHeight = surfaceHeight,
              .width = quadW,
              .height = tileH,
              .opacity = color.a,
              .transform = tileWorld,
          }
      );
    }
  }
}
