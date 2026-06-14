#include "render/text/glyph_registry.h"

#include "core/log.h"
#include "core/resource_paths.h"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>

namespace {

  constexpr Logger kLog("glyph-registry");
  constexpr char32_t kMissingGlyph = 0xF292; // tabler skull

  [[nodiscard]] std::optional<char32_t> parseCodepointLiteral(std::string_view value) {
    if (value.size() < 3) {
      return std::nullopt;
    }

    std::string_view hex;
    if ((value[0] == 'U' || value[0] == 'u') && value[1] == '+') {
      hex = value.substr(2);
    } else if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
      hex = value.substr(2);
    } else {
      return std::nullopt;
    }

    if (hex.empty()) {
      return std::nullopt;
    }

    std::uint32_t codepoint = 0;
    const auto* begin = hex.data();
    const auto* end = begin + hex.size();
    const auto result = std::from_chars(begin, end, codepoint, 16);
    if (result.ec != std::errc{} || result.ptr != end || codepoint == 0 || codepoint > 0x10FFFF) {
      return std::nullopt;
    }
    return static_cast<char32_t>(codepoint);
  }

  [[nodiscard]] std::unordered_map<std::string, char32_t> loadTablerIcons() {
    std::unordered_map<std::string, char32_t> icons;
    const std::filesystem::path path = paths::assetPath("fonts/tabler.json");
    std::ifstream file(path);
    if (!file.is_open()) {
      kLog.warn("failed to open Tabler glyph metadata: {}", path.string());
      return icons;
    }

    try {
      const auto root = nlohmann::json::parse(file);
      if (!root.is_object()) {
        kLog.warn("Tabler glyph metadata is not an object: {}", path.string());
        return icons;
      }

      icons.reserve(root.size());
      for (const auto& [name, value] : root.items()) {
        if (!value.is_string()) {
          continue;
        }
        if (auto parsed = parseCodepointLiteral(value.get<std::string>())) {
          icons.emplace(name, *parsed);
        }
      }
      kLog.info("loaded {} Tabler glyph names from {}", icons.size(), path.string());
    } catch (const nlohmann::json::exception& e) {
      kLog.warn("failed to parse Tabler glyph metadata '{}': {}", path.string(), e.what());
    }
    return icons;
  }

  [[nodiscard]] std::unordered_map<std::string, char32_t>& glyphs() {
    static std::unordered_map<std::string, char32_t> s_glyphs;
    return s_glyphs;
  }

} // namespace

void GlyphRegistry::initialize() { glyphs() = loadTablerIcons(); }

char32_t GlyphRegistry::lookup(std::string_view name) {
  if (auto codepoint = parseCodepointLiteral(name)) {
    return *codepoint;
  }

  const auto it = glyphs().find(std::string(name));
  if (it != glyphs().end()) {
    return it->second;
  }

  kLog.warn("missing glyph: {}", name);
  return kMissingGlyph;
}

void GlyphRegistry::registerGlyph(std::string_view name, char32_t codepoint) {
  glyphs()[std::string(name)] = codepoint;
}

std::optional<std::string> GlyphRegistry::fontPath() {
  const std::filesystem::path path = paths::assetPath("fonts/tabler.ttf");
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    kLog.warn("tabler.ttf not found at {}", path.string());
    return std::nullopt;
  }
  return path.string();
}
