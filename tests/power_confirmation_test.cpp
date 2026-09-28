#include "greeter/power_confirmation.h"

#undef NDEBUG
#include <cassert>
#include <initializer_list>

int main() {
  greeter::PowerConfirmation dialog;
  assert(!dialog.accept());
  for (auto action : {greeter::PowerAction::Shutdown, greeter::PowerAction::Reboot, greeter::PowerAction::Firmware}) {
    dialog.request(action);
    assert(dialog.active());
    dialog.cancel();
    assert(!dialog.active());
    assert(!dialog.accept());
    dialog.request(action);
    assert(dialog.accept() == action);
    assert(!dialog.accept());
  }
}
