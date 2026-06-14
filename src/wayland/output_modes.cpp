#include "wayland/output_modes.h"

#include <algorithm>
#include <cstdio>
#include <string>

std::string formatRefreshRateMHz(const int32_t refreshMHz) {
  if (refreshMHz <= 0) {
    return "?";
  }
  if (refreshMHz % 1000 == 0) {
    return std::to_string(refreshMHz / 1000);
  }
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(refreshMHz) / 1000.0);
  return buffer;
}

std::vector<int32_t>
uniqueRefreshRatesAtResolution(const std::vector<WaylandOutputMode>& modes, const int32_t width, const int32_t height) {
  std::vector<int32_t> refreshes;
  for (const auto& mode : modes) {
    if (mode.width != width || mode.height != height || mode.refreshMHz <= 0) {
      continue;
    }
    if (std::find(refreshes.begin(), refreshes.end(), mode.refreshMHz) == refreshes.end()) {
      refreshes.push_back(mode.refreshMHz);
    }
  }
  std::sort(refreshes.begin(), refreshes.end());
  return refreshes;
}
