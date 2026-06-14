#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct WaylandOutputMode {
  int32_t width = 0;
  int32_t height = 0;
  int32_t refreshMHz = 0;
};

[[nodiscard]] std::string formatRefreshRateMHz(int32_t refreshMHz);

[[nodiscard]] std::vector<int32_t>
uniqueRefreshRatesAtResolution(const std::vector<WaylandOutputMode>& modes, int32_t width, int32_t height);
