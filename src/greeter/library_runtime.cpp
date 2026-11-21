#include "greeter/library_runtime.h"

#include "core/application_identity.h"
#include "core/deferred_call.h"
#include "core/input/key_symbols.h"
#include "core/input/keybind_matcher.h"
#include "core/timer_manager.h"
#include "render/codecs/codecs.h"
#include "ui/controls/input.h"
#include "wayland/surface.h"

void initializeGreeterLibrary() {
  static const bool initialized = [] {
    setApplicationIdentity({.name = "noctalia-greeter", .envPrefix = "NOCTALIA_GREETER"});
    codecs::installImageDecoder();
    Input::setValidateKeyMatcher([](std::uint32_t sym, std::uint32_t) { return KeySymbol::isEnter(sym); });
    KeybindMatcher::setMatcher(KeybindAction::Validate, [](std::uint32_t sym, std::uint32_t) {
      return KeySymbol::isEnter(sym);
    });
    return true;
  }();
  (void)initialized;
}

void drainGreeterUiWork() {
  DeferredCall::drainWakeFd();
  for (auto& callback : DeferredCall::takePending())
    callback();
  TimerManager::instance().tick();
  Surface::drainPendingFrameWork();
  Surface::drainPendingRenders();
}

int greeterUiPollTimeoutMs() {
  if (Surface::hasPendingFrameWork() || Surface::hasPendingRenders())
    return 0;
  return TimerManager::instance().pollTimeoutMs();
}
