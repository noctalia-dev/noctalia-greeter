#pragma once
#include "ui/palette.h"

constexpr Palette completeGreeterPalette(Palette p) {
  p.primaryContainer = p.primary;
  p.onPrimaryContainer = p.onPrimary;
  p.secondaryContainer = p.secondary;
  p.onSecondaryContainer = p.onSecondary;
  p.tertiaryContainer = p.tertiary;
  p.onTertiaryContainer = p.onTertiary;
  p.errorContainer = p.error;
  p.onErrorContainer = p.onError;
  p.surfaceContainerLow = p.surface;
  p.surfaceContainerHigh = p.surfaceContainer;
  p.surfaceContainerHighest = p.surfaceContainer;
  p.scrim = p.shadow;
  return p;
}
