#pragma once

#include "render/core/color.h"

struct SysmonGraphStyle {
  Color lineColor1{};
  Color lineColor2{};
  float count1 = 0.0F;
  float count2 = 0.0F;
  float scroll1 = 0.0F;
  float scroll2 = 0.0F;
  float lineWidth = 0.75F;
  float graphFillOpacity = 0.2F;
  float aaSize = 0.5F;
};
