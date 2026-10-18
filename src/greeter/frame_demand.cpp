#include "greeter/frame_demand.h"

bool frameNeedsRedraw(const FrameDemand& demand) noexcept {
  return demand.redrawRequested
      || demand.layoutRequested
      || demand.sceneLayoutDirty
      || demand.scenePaintDirty
      || demand.animating;
}
